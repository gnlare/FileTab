// PaneView: 배치, 그리기, 마우스 처리, 주소 입력
#include <algorithm>
#include <cstdlib>

#include "PaneView.h"
#include <commdlg.h>
#include "SettingsService.h"

namespace ft {

namespace {
constexpr UINT_PTR kRefreshTimer = 1;
enum : UINT {
    kCmdCloseTab = 40,
    kCmdBmEdit = 50, kCmdBmDelete = 51,
    kCmdColorNone = 60, kCmdColorRed = 61, kCmdColorOrange = 62,
    kCmdColorYellow = 63, kCmdColorGreen = 64, kCmdColorBlue = 65,
    kCmdColorPurple = 66, kCmdColorCustom = 70
};

HBITMAP CreateColorMenuBitmap(COLORREF color, int w, int h) {
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP bmp = CreateCompatibleBitmap(screenDc, w, h);
    HGDIOBJ oldBmp = SelectObject(memDc, bmp);
    RECT rc{0, 0, w, h};
    if (color == CLR_INVALID) {
        ui::FillRectColor(memDc, rc, RGB(255, 255, 255));
        ui::FrameRectColor(memDc, rc, RGB(180, 180, 180));
        HPEN redPen = CreatePen(PS_SOLID, 1, RGB(239, 68, 68));
        HGDIOBJ oldPen = SelectObject(memDc, redPen);
        MoveToEx(memDc, 1, h - 2, nullptr);
        LineTo(memDc, w - 2, 1);
        SelectObject(memDc, oldPen);
        DeleteObject(redPen);
    } else {
        ui::FillRectColor(memDc, rc, color);
        ui::FrameRectColor(memDc, rc, RGB(100, 100, 100));
    }
    SelectObject(memDc, oldBmp);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return bmp;
}
}  // namespace

// ======================= 배치 =======================

void PaneView::Layout() {
    RECT cr{};
    GetClientRect(hwnd_, &cr);
    const int W = cr.right, H = cr.bottom;
    const int b = ui::S(4);
    RECT in{b, b, std::max(b, W - b), std::max(b, H - b)};
    tabStrip_ = {in.left, in.top, in.right, in.top + ui::S(26)};
    plusBtn_ = {tabStrip_.right - ui::S(22), tabStrip_.top + ui::S(3), tabStrip_.right - ui::S(2), tabStrip_.top + ui::S(23)};
    tabArea_ = {tabStrip_.left, tabStrip_.top, plusBtn_.left - ui::S(2), tabStrip_.bottom};
    const int y1 = tabStrip_.bottom + ui::S(2);
    backBtn_ = {in.left + ui::S(2), y1 + ui::S(1), in.left + ui::S(26), y1 + ui::S(25)};
    fwdBtn_ = {backBtn_.right + ui::S(2), backBtn_.top, backBtn_.right + ui::S(26), backBtn_.bottom};
    upBtn_ = {fwdBtn_.right + ui::S(2), backBtn_.top, fwdBtn_.right + ui::S(26), backBtn_.bottom};
    addrRect_ = {upBtn_.right + ui::S(6), y1, in.right - ui::S(2), y1 + ui::S(26)};
    const int y2 = y1 + ui::S(26) + ui::S(4);
    bmRow_ = {in.left, y2, in.right, y2 + ui::S(24)};
    groupBtn_ = {bmRow_.left + ui::S(1), y2 + ui::S(1), bmRow_.left + ui::S(23), y2 + ui::S(23)};
    starBtn_ = {bmRow_.right - ui::S(21), y2 + ui::S(2), bmRow_.right - ui::S(1), y2 + ui::S(22)};
    bmArea_ = {groupBtn_.right + ui::S(3), y2, std::max(groupBtn_.right + ui::S(3), starBtn_.left - ui::S(3)), y2 + ui::S(24)};
    statusRect_ = {in.left, std::max(in.top, in.bottom - ui::S(24)), in.right, in.bottom};
    fileRect_ = {in.left, bmRow_.bottom + ui::S(3), in.right, std::max(bmRow_.bottom + ui::S(3), statusRect_.top - ui::S(3))};
    if (files_) MoveWindow(files_->hwnd(), fileRect_.left, fileRect_.top, std::max(0, ui::RcW(fileRect_)), std::max(0, ui::RcH(fileRect_)), TRUE);
    if (editing_) {
        RECT r = addrRect_;
        InflateRect(&r, -ui::S(1), -ui::S(1));
        MoveWindow(edit_, r.left + ui::S(4), r.top + ui::S(3), std::max(0, ui::RcW(r) - ui::S(8)), std::max(0, ui::RcH(r) - ui::S(6)), TRUE);
    }
    if (tip_) {
        TOOLINFOW ti{};
        ti.cbSize = sizeof ti;
        ti.hwnd = hwnd_;
        ti.uId = 1;
        ti.rect = cr;
        SendMessageW(tip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&ti));
    }
    tabsDirty_ = bmDirty_ = crumbsDirty_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PaneView::EnsureTabLayout() {
    if (!tabsDirty_) return;
    tabsDirty_ = false;

    const int availMax = plusBtn_.left - ui::S(2) - tabStrip_.left;

    HDC dc = GetDC(hwnd_);
    tabRects_.clear();
    int x = tabArea_.left - tabScroll_;
    for (const auto& t : tabs_) {
        int textW = ui::TextWidth(dc, t->Title(), ui::fontBold);
        int w = std::min(ui::S(100), textW + ui::S(18));
        w = std::max(ui::S(36), w);
        tabRects_.push_back({x, tabStrip_.top + ui::S(2), x + w, tabStrip_.bottom});
        x += w + ui::S(1);
    }
    ReleaseDC(hwnd_, dc);
    tabsTotal_ = x - (tabArea_.left - tabScroll_);

    bool overflow = tabsTotal_ > availMax;
    if (overflow) {
        tabListBtn_ = {plusBtn_.left - ui::S(22), tabStrip_.top + ui::S(3), plusBtn_.left - ui::S(2), tabStrip_.top + ui::S(23)};
        tabArea_ = {tabStrip_.left, tabStrip_.top, tabListBtn_.left - ui::S(2), tabStrip_.bottom};
    } else {
        tabListBtn_ = {0, 0, 0, 0};
        tabArea_ = {tabStrip_.left, tabStrip_.top, plusBtn_.left - ui::S(2), tabStrip_.bottom};
    }

    int maxScroll = std::max(0, tabsTotal_ - ui::RcW(tabArea_));
    if (tabScroll_ > maxScroll || tabScroll_ < 0) {
        tabScroll_ = std::clamp(tabScroll_, 0, maxScroll);
        tabsDirty_ = true;
        EnsureTabLayout();
    }
}

void PaneView::EnsureTabVisible(int index) {
    EnsureTabLayout();
    if (index < 0 || index >= static_cast<int>(tabRects_.size())) return;
    const RECT& rc = tabRects_[static_cast<size_t>(index)];
    if (rc.left < tabArea_.left) tabScroll_ -= tabArea_.left - rc.left;
    else if (rc.right > tabArea_.right) tabScroll_ += rc.right - tabArea_.right;
    else return;
    tabScroll_ = std::max(0, tabScroll_);
    tabsDirty_ = true;
    EnsureTabLayout();
}

void PaneView::EnsureBookmarkLayout() {
    if (!bmDirty_) return;
    bmDirty_ = false;
    HDC dc = GetDC(hwnd_);
    bmRects_.clear();
    const auto& marks = SettingsService::Get().state.bookmarks;
    int x = bmArea_.left - bmScroll_;
    for (const auto& b : marks) {
        int w = std::min(ui::S(180), ui::TextWidth(dc, b.name, ui::font)) + ui::S(16);
        bmRects_.push_back({x, bmArea_.top + ui::S(1), x + w, bmArea_.bottom - ui::S(1)});
        x += w + ui::S(3);
    }
    ReleaseDC(hwnd_, dc);
    bmTotal_ = x - (bmArea_.left - bmScroll_);
    int maxScroll = std::max(0, bmTotal_ - ui::RcW(bmArea_));
    if (bmScroll_ > maxScroll || bmScroll_ < 0) {
        bmScroll_ = std::clamp(bmScroll_, 0, maxScroll);
        bmDirty_ = true;
        EnsureBookmarkLayout();
    }
}

void PaneView::BuildBreadcrumbs() {
    if (!crumbsDirty_) return;
    crumbsDirty_ = false;
    crumbs_.clear();
    auto tab = SelectedTab();
    if (!tab) return;
    auto parts = path::Breadcrumbs(tab->Path());
    if (parts.empty()) return;
    HDC dc = GetDC(hwnd_);
    const int n = static_cast<int>(parts.size());
    const int sepW = ui::S(14), padW = ui::S(8);
    std::vector<int> widths;
    int total = 0;
    for (const auto& p : parts) {
        widths.push_back(ui::TextWidth(dc, p.label, ui::font) + padW + sepW);
        total += widths.back();
    }
    const int ellW = ui::S(30);
    const int avail = ui::RcW(addrRect_) - ui::S(16);
    int start = 0;
    for (;;) {
        int need = total + (start > 0 ? ellW : 0);
        if (need <= avail || start >= n - 1) break;
        total -= widths[static_cast<size_t>(start)];
        ++start;
    }
    const int top = addrRect_.top + ui::S(3), bottom = addrRect_.bottom - ui::S(3);
    int x = addrRect_.left + ui::S(6);
    if (start > 0) {
        Crumb c;
        c.rc = {x, top, x + ellW - sepW, bottom};
        c.label = L"…";
        c.target = parts[static_cast<size_t>(start - 1)].path;
        c.ellipsis = true;
        crumbs_.push_back(c);
        x += ellW;
    }
    for (int i = start; i < n; ++i) {
        Crumb c;
        int w = widths[static_cast<size_t>(i)] - sepW;
        c.rc = {x, top, x + w, bottom};
        c.label = parts[static_cast<size_t>(i)].label;
        c.target = parts[static_cast<size_t>(i)].path;
        crumbs_.push_back(c);
        x += widths[static_cast<size_t>(i)];
    }
    ReleaseDC(hwnd_, dc);
}

PaneView::Hit PaneView::HitTestPane(int x, int y) {
    EnsureTabLayout();
    EnsureBookmarkLayout();
    BuildBreadcrumbs();
    if (ui::PtIn(plusBtn_, x, y)) return {HitKind::Plus, 0};
    if (ui::RcW(tabListBtn_) > 0 && ui::PtIn(tabListBtn_, x, y)) return {HitKind::TabList, 0};
    if (ui::PtIn(tabArea_, x, y)) {
        for (size_t i = 0; i < tabRects_.size(); ++i)
            if (ui::PtIn(tabRects_[i], x, y)) return {HitKind::Tab, static_cast<int>(i)};
        return {};
    }
    if (ui::PtIn(backBtn_, x, y)) return {HitKind::Back, 0};
    if (ui::PtIn(fwdBtn_, x, y)) return {HitKind::Forward, 0};
    if (ui::PtIn(upBtn_, x, y)) return {HitKind::Up, 0};
    if (!editing_ && ui::PtIn(addrRect_, x, y)) {
        for (size_t i = 0; i < crumbs_.size(); ++i)
            if (ui::PtIn(crumbs_[i].rc, x, y)) return {HitKind::Crumb, static_cast<int>(i)};
        return {HitKind::Address, 0};
    }
    if (ui::PtIn(groupBtn_, x, y)) return {HitKind::Group, 0};
    if (ui::PtIn(starBtn_, x, y)) return {HitKind::Star, 0};
    if (ui::PtIn(bmArea_, x, y)) {
        for (size_t i = 0; i < bmRects_.size(); ++i)
            if (ui::PtIn(bmRects_[i], x, y)) return {HitKind::Bookmark, static_cast<int>(i)};
    }
    return {};
}

bool PaneView::IsOverBookmarkRow(POINT p) const { return ui::PtIn(bmRow_, p.x, p.y); }

std::wstring PaneView::TooltipFor(int x, int y) {
    Hit h = HitTestPane(x, y);
    switch (h.kind) {
        case HitKind::Tab:
            return (h.index >= 0 && h.index < static_cast<int>(tabs_.size())) ? tabs_[static_cast<size_t>(h.index)]->Path() : L"";
        case HitKind::Bookmark: {
            const auto& marks = SettingsService::Get().state.bookmarks;
            return (h.index >= 0 && h.index < static_cast<int>(marks.size())) ? marks[static_cast<size_t>(h.index)].path : L"";
        }
        case HitKind::TabList: return L"열린 탭 목록";
        case HitKind::Plus: return L"새 탭 (Ctrl+T)";
        case HitKind::Back: return L"뒤로 (Alt+←)";
        case HitKind::Forward: return L"앞으로 (Alt+→)";
        case HitKind::Up: return L"상위 폴더 (Alt+↑)";
        case HitKind::Group: return L"폴더 그룹";
        case HitKind::Star: return L"현재 폴더를 북마크에 추가";
        default: return L"";
    }
}

// ======================= 그리기 =======================

void PaneView::DrawButton(HDC dc, const RECT& rc, const wchar_t* label, bool enabled, bool hot, bool pressed) {
    COLORREF bg = !enabled ? ui::kButtonBg : pressed ? ui::kPressed : hot ? ui::kHover : ui::kButtonBg;
    ui::FillRectColor(dc, rc, bg);
    ui::FrameRectColor(dc, rc, ui::kButtonBorder);
    ui::DrawTextIn(dc, label, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE, enabled ? ui::kText : RGB(0xA6, 0xAE, 0xBB));
}

void PaneView::Paint() {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(hwnd_, &ps);
    RECT cr{};
    GetClientRect(hwnd_, &cr);
    const int W = cr.right, H = cr.bottom;
    if (W > 0 && H > 0) {
        if (!memDc_) memDc_ = CreateCompatibleDC(wdc);
        if (!backBmp_ || backW_ != W || backH_ != H) {
            if (backBmp_) {
                SelectObject(memDc_, backOld_);
                DeleteObject(backBmp_);
            }
            backBmp_ = CreateCompatibleBitmap(wdc, W, H);
            backW_ = W;
            backH_ = H;
            backOld_ = static_cast<HBITMAP>(SelectObject(memDc_, backBmp_));
        }
        HDC dc = memDc_;
        EnsureTabLayout();
        EnsureBookmarkLayout();
        BuildBreadcrumbs();
        auto tab = SelectedTab();
        SetBkMode(dc, TRANSPARENT);

        ui::FillRectColor(dc, cr, ui::kPanelBg);
        ui::FrameRectColor(dc, cr, active_ ? ui::kAccent : ui::kBorderIdle, ui::S(2));

        // 탭 줄
        ui::FillRectColor(dc, tabStrip_, ui::kStripBg);
        HRGN clip = CreateRectRgn(tabArea_.left, tabArea_.top, tabArea_.right, tabArea_.bottom);
        SelectClipRgn(dc, clip);
        for (size_t i = 0; i < tabRects_.size() && i < tabs_.size(); ++i) {
            const RECT& rc = tabRects_[i];
            if (rc.right < tabArea_.left || rc.left > tabArea_.right) continue;
            const bool sel = static_cast<int>(i) == selected_;
            const bool hot = hot_.kind == HitKind::Tab && hot_.index == static_cast<int>(i);
            if (sel) ui::FillRectColor(dc, rc, ui::kPanelBg);
            else if (hot) ui::FillRectColor(dc, rc, ui::kHover);
            if (tabs_[i]->HasColor()) {
                RECT line{rc.left, rc.top, rc.right, rc.top + ui::S(3)};
                ui::FillRectColor(dc, line, tabs_[i]->Color());
            }
            RECT tr{rc.left + ui::S(6), rc.top, rc.right - ui::S(6), rc.bottom};
            ui::DrawTextIn(dc, tabs_[i]->Title(), tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, ui::kText, sel ? ui::fontBold : ui::font);
        }
        SelectClipRgn(dc, nullptr);
        DeleteObject(clip);
        DrawButton(dc, plusBtn_, L"+", true, hot_.kind == HitKind::Plus, pressed_.kind == HitKind::Plus);
        if (ui::RcW(tabListBtn_) > 0) {
            DrawButton(dc, tabListBtn_, L"▼", true, hot_.kind == HitKind::TabList, pressed_.kind == HitKind::TabList);
        }

        // 이동 버튼
        const bool canBack = tab && tab->CanBack(), canFwd = tab && tab->CanForward(), canUp = tab && !path::Parent(tab->Path()).empty();
        DrawButton(dc, backBtn_, L"←", canBack, hot_.kind == HitKind::Back, pressed_.kind == HitKind::Back);
        DrawButton(dc, fwdBtn_, L"→", canFwd, hot_.kind == HitKind::Forward, pressed_.kind == HitKind::Forward);
        DrawButton(dc, upBtn_, L"↑", canUp, hot_.kind == HitKind::Up, pressed_.kind == HitKind::Up);

        // 주소 줄(이동 경로)
        ui::FillRectColor(dc, addrRect_, ui::kFieldBg);
        ui::FrameRectColor(dc, addrRect_, ui::kFieldBorder);
        if (!editing_) {
            for (size_t i = 0; i < crumbs_.size(); ++i) {
                const Crumb& c = crumbs_[i];
                const bool hot = hot_.kind == HitKind::Crumb && hot_.index == static_cast<int>(i);
                if (hot) ui::FillRectColor(dc, c.rc, pressed_.kind == HitKind::Crumb && pressed_.index == hot_.index ? ui::kPressed : ui::kHover);
                ui::DrawTextIn(dc, c.label, c.rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE, ui::kText);
                if (i + 1 < crumbs_.size()) {
                    RECT sep{c.rc.right, c.rc.top, c.rc.right + ui::S(14), c.rc.bottom};
                    ui::DrawTextIn(dc, L"›", sep, DT_CENTER | DT_VCENTER | DT_SINGLELINE, ui::kTextDim);
                }
            }
        }

        // 북마크 줄
        ui::FillRectColor(dc, bmRow_, ui::kStripBg);
        DrawButton(dc, groupBtn_, L"📁", true, hot_.kind == HitKind::Group, pressed_.kind == HitKind::Group);
        DrawButton(dc, starBtn_, L"★", true, hot_.kind == HitKind::Star, pressed_.kind == HitKind::Star);
        HRGN bmClip = CreateRectRgn(bmArea_.left, bmArea_.top, bmArea_.right, bmArea_.bottom);
        SelectClipRgn(dc, bmClip);
        const auto& marks = SettingsService::Get().state.bookmarks;
        for (size_t i = 0; i < bmRects_.size() && i < marks.size(); ++i) {
            if (bmRects_[i].right < bmArea_.left || bmRects_[i].left > bmArea_.right) continue;
            RECT lr = bmRects_[i];
            DrawButton(dc, lr, L"", true, hot_.kind == HitKind::Bookmark && hot_.index == static_cast<int>(i),
                       pressed_.kind == HitKind::Bookmark && pressed_.index == static_cast<int>(i));
            InflateRect(&lr, -ui::S(6), 0);
            ui::DrawTextIn(dc, marks[i].name, lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, ui::kText);
        }
        SelectClipRgn(dc, nullptr);
        DeleteObject(bmClip);

        // 상태 줄
        std::wstring status;
        if (files_ && files_->SelectedCount() == 1) status = files_->FirstSelectedName();
        else if (files_ && files_->SelectedCount() > 1) status = files_->FirstSelectedName() + L"...";
        else if (tab) status = tab->Status();
        RECT sr = statusRect_;
        sr.left += ui::S(6);
        ui::DrawTextIn(dc, status, sr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, ui::kStatusText);

        BitBlt(wdc, 0, 0, W, H, dc, 0, 0, SRCCOPY);
    }
    EndPaint(hwnd_, &ps);
}

// ======================= 입력 =======================

void PaneView::OnLButtonDown(int x, int y) {
    if (cb.activated) cb.activated(this);
    Hit h = HitTestPane(x, y);
    if (h.kind == HitKind::Address) {
        EditAddress();
        return;
    }
    pressed_ = h;
    if (h.kind == HitKind::Tab) {
        SelectTab(h.index);
        tabDrag_ = h.index;
        tabDownX_ = x;
        tabDragging_ = false;
    }
    if (h.kind != HitKind::None) SetCapture(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PaneView::OnMouseMove(int x, int y) {
    if (!leaveTracked_) {
        TRACKMOUSEEVENT t{};
        t.cbSize = sizeof t;
        t.dwFlags = TME_LEAVE;
        t.hwndTrack = hwnd_;
        TrackMouseEvent(&t);
        leaveTracked_ = true;
    }
    if (tabDrag_ >= 0 && GetKeyState(VK_LBUTTON) < 0) {
        if (!tabDragging_ && std::abs(x - tabDownX_) >= GetSystemMetrics(SM_CXDRAG)) tabDragging_ = true;
        if (tabDragging_) {
            EnsureTabLayout();
            int target = -1;
            for (size_t i = 0; i < tabRects_.size(); ++i)
                if (x >= tabRects_[i].left && x < tabRects_[i].right) target = static_cast<int>(i);
            if (target >= 0 && target != tabDrag_) {
                auto moved = tabs_[static_cast<size_t>(tabDrag_)];
                tabs_.erase(tabs_.begin() + tabDrag_);
                tabs_.insert(tabs_.begin() + target, moved);
                selected_ = target;   // 끌고 있는 탭은 선택된 탭이다
                tabDrag_ = target;
                tabsDirty_ = true;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
        return;
    }
    Hit h = HitTestPane(x, y);
    if (h != hot_) {
        hot_ = h;
        if (tip_) SendMessageW(tip_, TTM_POP, 0, 0);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void PaneView::Click(const Hit& hit) {
    auto tab = SelectedTab();
    switch (hit.kind) {
        case HitKind::Plus: NewTab(); break;
        case HitKind::TabList: ShowTabListMenu(); break;
        case HitKind::Back: if (tab && tab->CanBack()) tab->Back(); break;
        case HitKind::Forward: if (tab && tab->CanForward()) tab->Forward(); break;
        case HitKind::Up: Up(); break;
        case HitKind::Group: ShowFolderGroupMenu(); break;
        case HitKind::Star: AddCurrentBookmark(); break;
        case HitKind::Bookmark: {
            const auto& marks = SettingsService::Get().state.bookmarks;
            if (hit.index >= 0 && hit.index < static_cast<int>(marks.size())) NavigateSelected(marks[static_cast<size_t>(hit.index)].path);
            break;
        }
        case HitKind::Crumb:
            if (hit.index >= 0 && hit.index < static_cast<int>(crumbs_.size()) && tab) {
                const auto& target = crumbs_[static_cast<size_t>(hit.index)].target;
                if (EqualsNoCase(target, path::Parent(tab->Path()))) Up();
                else NavigateSelected(target);
            }
            break;
        default: break;
    }
}

void PaneView::ShowTabListMenu() {
    if (tabs_.empty()) return;
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < tabs_.size(); ++i) {
        std::wstring title = tabs_[i]->Title();
        if (title.empty()) title = tabs_[i]->Path();
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(1000 + i), title.c_str());
        if (static_cast<int>(i) == selected_) {
            CheckMenuItem(menu, static_cast<UINT>(1000 + i), MF_BYCOMMAND | MF_CHECKED);
        }
    }
    POINT pt{tabListBtn_.left, tabListBtn_.bottom};
    ClientToScreen(hwnd_, &pt);
    UINT cmd = static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, hwnd_, nullptr));
    DestroyMenu(menu);
    if (cmd >= 1000) {
        int idx = static_cast<int>(cmd - 1000);
        if (idx >= 0 && idx < static_cast<int>(tabs_.size())) {
            SelectTab(idx);
            EnsureTabVisible(idx);
        }
    }
}

void PaneView::OnLButtonUp(int x, int y) {
    const bool wasDrag = tabDragging_;
    Hit was = pressed_;
    pressed_ = Hit{};
    tabDrag_ = -1;
    tabDragging_ = false;
    if (GetCapture() == hwnd_) ReleaseCapture();
    Hit now = HitTestPane(x, y);
    if (!wasDrag && was.kind != HitKind::None && was == now && was.kind != HitKind::Tab) Click(now);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PaneView::OnRButtonUp(int x, int y) {
    Hit h = HitTestPane(x, y);
    POINT pt{x, y};
    ClientToScreen(hwnd_, &pt);
    if (h.kind == HitKind::Tab && h.index >= 0 && h.index < static_cast<int>(tabs_.size())) {
        SelectTab(h.index);
        auto currentTab = tabs_[static_cast<size_t>(h.index)];
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, kCmdCloseTab, L"탭 닫기\tCtrl+W");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        HMENU colorMenu = CreatePopupMenu();
        AppendMenuW(colorMenu, MF_STRING, kCmdColorNone, currentTab->HasColor() ? L"색 없음" : L"색 없음 (현재)");
        AppendMenuW(colorMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(colorMenu, MF_STRING, kCmdColorRed, L"빨강");
        AppendMenuW(colorMenu, MF_STRING, kCmdColorOrange, L"주황");
        AppendMenuW(colorMenu, MF_STRING, kCmdColorYellow, L"노랑");
        AppendMenuW(colorMenu, MF_STRING, kCmdColorGreen, L"초록");
        AppendMenuW(colorMenu, MF_STRING, kCmdColorBlue, L"파랑");
        AppendMenuW(colorMenu, MF_STRING, kCmdColorPurple, L"보라");
        AppendMenuW(colorMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(colorMenu, MF_STRING, kCmdColorCustom, L"색 설정...");

        int sz = std::clamp(GetSystemMetrics(SM_CYMENUCHECK), 12, 18);
        HBITMAP bmpNone = CreateColorMenuBitmap(CLR_INVALID, sz, sz);
        HBITMAP bmpRed = CreateColorMenuBitmap(RGB(239, 68, 68), sz, sz);
        HBITMAP bmpOrange = CreateColorMenuBitmap(RGB(249, 115, 22), sz, sz);
        HBITMAP bmpYellow = CreateColorMenuBitmap(RGB(234, 179, 8), sz, sz);
        HBITMAP bmpGreen = CreateColorMenuBitmap(RGB(16, 185, 129), sz, sz);
        HBITMAP bmpBlue = CreateColorMenuBitmap(RGB(59, 130, 246), sz, sz);
        HBITMAP bmpPurple = CreateColorMenuBitmap(RGB(139, 92, 246), sz, sz);
        HBITMAP bmpCustom = CreateColorMenuBitmap(currentTab->HasColor() ? currentTab->Color() : RGB(200, 200, 200), sz, sz);

        SetMenuItemBitmaps(colorMenu, kCmdColorNone, MF_BYCOMMAND, bmpNone, bmpNone);
        SetMenuItemBitmaps(colorMenu, kCmdColorRed, MF_BYCOMMAND, bmpRed, bmpRed);
        SetMenuItemBitmaps(colorMenu, kCmdColorOrange, MF_BYCOMMAND, bmpOrange, bmpOrange);
        SetMenuItemBitmaps(colorMenu, kCmdColorYellow, MF_BYCOMMAND, bmpYellow, bmpYellow);
        SetMenuItemBitmaps(colorMenu, kCmdColorGreen, MF_BYCOMMAND, bmpGreen, bmpGreen);
        SetMenuItemBitmaps(colorMenu, kCmdColorBlue, MF_BYCOMMAND, bmpBlue, bmpBlue);
        SetMenuItemBitmaps(colorMenu, kCmdColorPurple, MF_BYCOMMAND, bmpPurple, bmpPurple);
        SetMenuItemBitmaps(colorMenu, kCmdColorCustom, MF_BYCOMMAND, bmpCustom, bmpCustom);

        if (!currentTab->HasColor()) CheckMenuItem(colorMenu, kCmdColorNone, MF_BYCOMMAND | MF_CHECKED);
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(colorMenu), L"탭 색 지정");

        UINT cmd = static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, hwnd_, nullptr));
        DestroyMenu(menu);

        DeleteObject(bmpNone);
        DeleteObject(bmpRed);
        DeleteObject(bmpOrange);
        DeleteObject(bmpYellow);
        DeleteObject(bmpGreen);
        DeleteObject(bmpBlue);
        DeleteObject(bmpPurple);
        DeleteObject(bmpCustom);

        if (cmd == kCmdCloseTab) {
            CloseTab(currentTab);
        } else if (cmd == kCmdColorNone) {
            currentTab->SetColor(CLR_INVALID);
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (cmd == kCmdColorRed) {
            currentTab->SetColor(RGB(239, 68, 68));
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (cmd == kCmdColorOrange) {
            currentTab->SetColor(RGB(249, 115, 22));
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (cmd == kCmdColorYellow) {
            currentTab->SetColor(RGB(234, 179, 8));
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (cmd == kCmdColorGreen) {
            currentTab->SetColor(RGB(16, 185, 129));
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (cmd == kCmdColorBlue) {
            currentTab->SetColor(RGB(59, 130, 246));
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (cmd == kCmdColorPurple) {
            currentTab->SetColor(RGB(139, 92, 246));
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (cmd == kCmdColorCustom) {
            CHOOSECOLORW cc{};
            static COLORREF custColors[16] = {};
            cc.lStructSize = sizeof(cc);
            cc.hwndOwner = hwnd_;
            cc.lpCustColors = custColors;
            cc.rgbResult = currentTab->HasColor() ? currentTab->Color() : RGB(59, 130, 246);
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                currentTab->SetColor(cc.rgbResult);
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
    } else if (h.kind == HitKind::Bookmark) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, kCmdBmEdit, L"북마크 수정");
        AppendMenuW(menu, MF_STRING, kCmdBmDelete, L"북마크 삭제");
        UINT cmd = static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, hwnd_, nullptr));
        DestroyMenu(menu);
        if (cmd == kCmdBmEdit) EditBookmark(h.index);
        else if (cmd == kCmdBmDelete) DeleteBookmark(h.index);
    }
}

// ======================= 주소 입력 =======================

void PaneView::EditAddress() {
    auto tab = SelectedTab();
    if (!tab) return;
    editing_ = true;
    SetWindowTextW(edit_, tab->Path().c_str());
    RECT r = addrRect_;
    InflateRect(&r, -ui::S(1), -ui::S(1));
    MoveWindow(edit_, r.left + ui::S(4), r.top + ui::S(3), std::max(0, ui::RcW(r) - ui::S(8)), std::max(0, ui::RcH(r) - ui::S(6)), TRUE);
    ShowWindow(edit_, SW_SHOW);
    SetFocus(edit_);
    SendMessageW(edit_, EM_SETSEL, 0, -1);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PaneView::ExitAddress() {
    if (!editing_) return;
    editing_ = false;
    ShowWindow(edit_, SW_HIDE);
    crumbsDirty_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PaneView::CommitAddress() {
    auto tab = SelectedTab();
    if (!tab) return;
    std::wstring text = Trim(ui::GetText(edit_));
    if (text.empty()) return;
    std::wstring normalized, error;
    if (path::TryNormalize(text, tab->Path(), normalized, &error)) {
        // 바로 가기 파일 경로를 입력하면 대상 폴더로 이동한다
        if (EqualsNoCase(path::Extension(normalized), L".lnk") && PathExists(normalized) && !IsDirectoryPath(normalized)) {
            std::wstring target = ResolveShortcutTarget(normalized);
            if (!target.empty() && IsDirectoryPath(target)) text = target;
        }
    }
    tab->Navigate(text, [this](bool ok) {
        if (ok && !disposed_) {
            ExitAddress();
            FocusFiles();
        }
    });
}

// ======================= 창 프로시저 =======================

LRESULT PaneView::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: Paint(); return 0;
        case WM_SIZE: Layout(); return 0;
        case WM_SETFOCUS:
            if (!editing_) FocusFiles();
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_LBUTTONUP: OnLButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_MOUSEMOVE: OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_RBUTTONUP: OnRButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_MBUTTONUP: {
            Hit h = HitTestPane(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (h.kind == HitKind::Tab && h.index >= 0 && h.index < static_cast<int>(tabs_.size())) CloseTab(tabs_[static_cast<size_t>(h.index)]);
            return 0;
        }
        case WM_MOUSELEAVE:
            leaveTracked_ = false;
            hot_ = Hit{};
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lParam) != hwnd_) {
                pressed_ = Hit{};
                tabDrag_ = -1;
                tabDragging_ = false;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEWHEEL: {
            POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd_, &p);
            const int step = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * ui::S(60);
            if (ui::PtIn(tabStrip_, p.x, p.y)) {
                tabScroll_ = std::max(0, tabScroll_ - step);
                tabsDirty_ = true;
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else if (ui::PtIn(bmRow_, p.x, p.y)) {
                bmScroll_ = std::max(0, bmScroll_ - step);
                bmDirty_ = true;
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else if (files_) {
                SendMessageW(files_->hwnd(), WM_MOUSEWHEEL, wParam, lParam);
            }
            return 0;
        }
        case WM_TIMER:
            if (wParam == kRefreshTimer) {
                auto tab = SelectedTab();
                if (tab && tab->IsBusy()) return 0;   // 읽는 중이면 다음 주기에 다시 확인
                KillTimer(hwnd_, kRefreshTimer);
                refreshTimerOn_ = false;
                if (watchPending_) {
                    watchPending_ = false;
                    Refresh();
                }
                return 0;
            }
            break;
        case WM_MENUCOMMAND:
            if (reinterpret_cast<HMENU>(lParam) == groupMenu_) OnGroupMenuCommand(static_cast<int>(wParam));
            return 0;
        case WM_MENURBUTTONUP:
            if (reinterpret_cast<HMENU>(lParam) == groupMenu_) OnGroupMenuRightClick(static_cast<int>(wParam));
            return 0;
        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lParam);
            if (hdr && hdr->hwndFrom == tip_ && hdr->code == TTN_GETDISPINFOW) {
                POINT p;
                GetCursorPos(&p);
                ScreenToClient(hwnd_, &p);
                tipText_ = TooltipFor(p.x, p.y);
                reinterpret_cast<NMTTDISPINFOW*>(lParam)->lpszText = tipText_.empty() ? const_cast<LPWSTR>(L"") : tipText_.data();
                return 0;
            }
            break;
        }
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

}  // namespace ft
