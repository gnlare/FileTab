#include "FileView.h"
#include "core/GridNavigation.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_set>

#include "FsServices.h"
#include "SettingsService.h"

namespace ft {

namespace {
const wchar_t* kClassName = L"FileTabFileView";

int ViewHeight(ViewMode mode, int clientH, int headerH) { return mode == ViewMode::Details ? std::max(0, clientH - headerH) : clientH; }
}  // namespace

bool FileView::RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &FileView::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    return RegisterClassExW(&wc) != 0;
}

FileView* FileView::Create(HWND parent, Host* host) {
    auto* view = new FileView();
    view->host_ = host;
    view->hwnd_ = CreateWindowExW(0, kClassName, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, 10,
                                  10, parent, nullptr, GetModuleHandleW(nullptr), view);
    if (!view->hwnd_) {
        delete view;
        return nullptr;
    }
    // 항목 경로 툴팁(마우스가 멈추면 전체 경로를 보여 준다)
    view->tip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT,
                                 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, view->hwnd_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (view->tip_) {
        TOOLINFOW ti{};
        ti.cbSize = sizeof ti;
        ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = view->hwnd_;
        ti.uId = 1;
        ti.lpszText = LPSTR_TEXTCALLBACKW;
        SendMessageW(view->tip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
        SendMessageW(view->tip_, TTM_SETMAXTIPWIDTH, 0, 700);
    }
    view->ComputeLayout();
    return view;
}

FileView::~FileView() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
    if (memDc_) {
        if (backOld_) SelectObject(memDc_, backOld_);
        DeleteDC(memDc_);
    }
    if (scratchDc_) DeleteDC(scratchDc_);
    if (backBmp_) DeleteObject(backBmp_);
}

LRESULT CALLBACK FileView::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    FileView* self;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<FileView*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<FileView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->HandleMessage(msg, wParam, lParam);
}

// ======================= 배치 / 스크롤 =======================

void FileView::ComputeLayout() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    cw_ = rc.right;
    ch_ = rc.bottom;
    m_.rowH = ui::S(30);
    m_.headerH = ui::S(26);
    m_.nameIcon = ui::S(20);
    m_.iconPx = mode_ == ViewMode::LargeIcons ? 128 : 64;   // 실제 화면 픽셀 기준(WPF 버전과 같은 규칙)
    const IconMargins& mg = SettingsService::Get().Margins(mode_);
    m_.tileW = m_.iconPx + ui::S(16 + 2 * mg.horizontal);
    m_.tileH = m_.iconPx + ui::S(32 + 2 * mg.vertical);
    cols_ = std::max(1, cw_ / std::max(1, m_.tileW));
    scrollY_ = std::clamp(scrollY_, 0, MaxScroll());
    UpdateScrollBar();
    if (tip_) {
        TOOLINFOW ti{};
        ti.cbSize = sizeof ti;
        ti.hwnd = hwnd_;
        ti.uId = 1;
        ti.rect = rc;
        SendMessageW(tip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&ti));
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

int FileView::ContentHeight() const {
    int n = ItemCount();
    if (mode_ == ViewMode::Details) return n * m_.rowH;
    return ((n + cols_ - 1) / cols_) * m_.tileH;
}

int FileView::MaxScroll() const { return std::max(0, ContentHeight() - ViewHeight(mode_, ch_, m_.headerH)); }

RECT FileView::ThumbRect() const {
    if (MaxScroll() <= 0) return {};
    int vh = ViewHeight(mode_, ch_, m_.headerH);
    int thH = std::min(vh, std::max(ui::S(24), static_cast<int>(static_cast<int64_t>(vh) * vh / std::max(1, ContentHeight()))));
    int trackH = std::max(1, vh - thH);
    int thY = (mode_ == ViewMode::Details ? m_.headerH : 0) + static_cast<int>(static_cast<int64_t>(trackH) * scrollY_ / MaxScroll());
    return {cw_ - ui::S(7), thY, cw_ - ui::S(2), thY + thH};
}

void FileView::UpdateScrollBar() {
    ShowScrollBar(hwnd_, SB_VERT, FALSE);
}

void FileView::SetScroll(int y) {
    int clamped = std::clamp(y, 0, MaxScroll());
    bool changed = clamped != scrollY_;
    scrollY_ = clamped;
    UpdateScrollBar();
    if (changed) InvalidateRect(hwnd_, nullptr, FALSE);
}

RECT FileView::ItemRect(int i) const {
    if (mode_ == ViewMode::Details) {
        int y = m_.headerH + i * m_.rowH - scrollY_;
        return {0, y, cw_, y + m_.rowH};
    }
    int x = (i % cols_) * m_.tileW;
    int y = (i / cols_) * m_.tileH - scrollY_;
    return {x, y, x + m_.tileW, y + m_.tileH};
}

void FileView::EnsureVisible(int i) {
    if (i < 0 || i >= ItemCount()) return;
    int unit = mode_ == ViewMode::Details ? m_.rowH : m_.tileH;
    int row = mode_ == ViewMode::Details ? i : i / cols_;
    int top = row * unit, bottom = top + unit;
    int viewH = ViewHeight(mode_, ch_, m_.headerH);
    if (top < scrollY_) SetScroll(top);
    else if (bottom > scrollY_ + viewH) SetScroll(bottom - viewH);
}

int FileView::HitTest(int x, int y) const {
    int n = ItemCount();
    if (n == 0 || x < 0 || y < 0) return -1;
    if (mode_ == ViewMode::Details) {
        if (y < m_.headerH || x >= cw_) return -1;
        int idx = (y - m_.headerH + scrollY_) / m_.rowH;
        return idx < n ? idx : -1;
    }
    if (x >= cols_ * m_.tileW) return -1;
    int idx = ((y + scrollY_) / m_.tileH) * cols_ + x / m_.tileW;
    return idx < n ? idx : -1;
}

void FileView::ColumnLayout(int& nameW, int& sizeW, int& modW) const {
    sizeW = ui::S(sizeColDip_);
    modW = ui::S(modColDip_);
    nameW = std::max(ui::S(kNameMinDip), cw_ - sizeW - modW);
}

int FileView::HeaderDivider(int x) const {
    int nameW, sizeW, modW;
    ColumnLayout(nameW, sizeW, modW);
    int grab = ui::S(4);
    if (std::abs(x - nameW) <= grab) return 1;
    if (std::abs(x - (nameW + sizeW)) <= grab) return 2;
    return 0;
}

int FileView::HeaderColumn(int x) const {
    int nameW, sizeW, modW;
    ColumnLayout(nameW, sizeW, modW);
    if (x < nameW) return 0;
    if (x < nameW + sizeW) return 1;
    return 2;
}

// ======================= 선택 =======================

void FileView::ClearSelection() {
    std::fill(sel_.begin(), sel_.end(), static_cast<char>(0));
    selCount_ = 0;
}

void FileView::SelectOnly(int i) {
    ClearSelection();
    if (i >= 0 && i < ItemCount()) {
        sel_[static_cast<size_t>(i)] = 1;
        selCount_ = 1;
    }
}

void FileView::SelectRange(int a, int b) {
    if (a > b) std::swap(a, b);
    a = std::max(a, 0);
    b = std::min(b, ItemCount() - 1);
    for (int k = a; k <= b; ++k)
        if (!sel_[static_cast<size_t>(k)]) {
            sel_[static_cast<size_t>(k)] = 1;
            ++selCount_;
        }
}

void FileView::ToggleSelect(int i) {
    if (i < 0 || i >= ItemCount()) return;
    char& s = sel_[static_cast<size_t>(i)];
    s = s ? 0 : 1;
    selCount_ += s ? 1 : -1;
}

void FileView::NotifySelection() {
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (host_) host_->FvSelectionChanged();
}

void FileView::MoveFocus(int target, bool extend, bool ctrlOnly) {
    int n = ItemCount();
    if (n == 0) return;
    target = std::clamp(target, 0, n - 1);
    if (extend) {
        if (anchor_ < 0) anchor_ = focus_ >= 0 ? focus_ : target;
        ClearSelection();
        SelectRange(anchor_, target);
    } else if (!ctrlOnly) {
        SelectOnly(target);
        anchor_ = target;
    }
    focus_ = target;
    EnsureVisible(target);
    NotifySelection();
}

std::vector<FileEntry> FileView::SelectedEntries() const {
    std::vector<FileEntry> result;
    const size_t count = std::min(items_ ? items_->size() : 0, sel_.size());
    for (size_t i = 0; i < count; ++i)
        if (sel_[i]) result.push_back((*items_)[i]);
    return result;
}

std::wstring FileView::FirstSelectedName() const {
    const size_t count = std::min(items_ ? items_->size() : 0, sel_.size());
    for (size_t i = 0; i < count; ++i)
        if (sel_[i]) return (*items_)[i].name;
    return std::wstring();
}

std::wstring FileView::FullPath(const FileEntry& e) const { return path::Combine(dir_, e.name); }

std::vector<std::wstring> FileView::SelectedPaths() const {
    std::vector<std::wstring> result;
    const size_t count = std::min(items_ ? items_->size() : 0, sel_.size());
    for (size_t i = 0; i < count; ++i)
        if (sel_[i]) result.push_back(FullPath((*items_)[i]));
    return result;
}

void FileView::RestoreSelection(const std::vector<std::wstring>& names, const std::wstring& focusName) {
    int n = ItemCount();
    sel_.assign(static_cast<size_t>(n), 0);
    selCount_ = 0;
    anchor_ = focus_ = -1;
    if (names.empty() && focusName.empty()) return;
    std::unordered_set<std::wstring> wanted(names.begin(), names.end());
    for (int i = 0; i < n; ++i) {
        const std::wstring& name = (*items_)[static_cast<size_t>(i)].name;
        if (wanted.count(name)) {
            sel_[static_cast<size_t>(i)] = 1;
            ++selCount_;
        }
        if (!focusName.empty() && name == focusName) focus_ = i;
    }
    anchor_ = focus_;
}

void FileView::SelectOnlyByName(const std::wstring& name) {
    for (int i = 0; i < ItemCount(); ++i)
        if ((*items_)[static_cast<size_t>(i)].name == name) {
            SelectOnly(i);
            anchor_ = focus_ = i;
            EnsureVisible(i);
            NotifySelection();
            return;
        }
}

bool FileView::HasFocus() const { return GetFocus() == hwnd_; }
void FileView::Focus() { SetFocus(hwnd_); }
void FileView::RepaintNow() { InvalidateRect(hwnd_, nullptr, FALSE); }

// ======================= 탭 연결 =======================

void FileView::SaveStateToTab() {
    if (!tab_) return;
    tab_->savedSelection.clear();
    const size_t count = std::min(items_ ? items_->size() : 0, sel_.size());
    for (size_t i = 0; i < count; ++i)
        if (sel_[i]) tab_->savedSelection.push_back((*items_)[i].name);
    tab_->savedFocus = (focus_ >= 0 && focus_ < ItemCount()) ? (*items_)[static_cast<size_t>(focus_)].name : std::wstring();
    tab_->savedScroll = scrollY_;
}

void FileView::SetTab(std::shared_ptr<FolderTab> tab) {
    if (tab_) SaveStateToTab();
    tab_ = std::move(tab);
    typeBuf_.clear();
    if (!tab_) {
        items_.reset();
        dir_.clear();
        sel_.clear();
        selCount_ = 0;
        focus_ = anchor_ = -1;
        scrollY_ = 0;
        ComputeLayout();
        NotifySelection();
        return;
    }
    items_ = tab_->Entries();
    dir_ = tab_->Path();
    mode_ = tab_->Mode();
    RestoreSelection(tab_->savedSelection, tab_->savedFocus);
    scrollY_ = tab_->savedScroll;
    ComputeLayout();
    NotifySelection();
}

void FileView::OnTabChanged(unsigned what) {
    if (!tab_) return;
    if (what & kChangeMode) {
        mode_ = tab_->Mode();
        ComputeLayout();
        if (focus_ >= 0) EnsureVisible(focus_);
    }
    if (what & kChangeEntries) {
        const int unit = mode_ == ViewMode::Details ? m_.rowH : m_.tileH;
        auto rowOf = [&](int idx) { return mode_ == ViewMode::Details ? idx : idx / cols_; };
        // 바뀌기 전 상태를 이름으로 기억한다.
        std::vector<std::wstring> names;
        const size_t oldCount = std::min(items_ ? items_->size() : 0, sel_.size());
        for (size_t i = 0; i < oldCount; ++i)
            if (sel_[i]) names.push_back((*items_)[i].name);
        std::wstring focusName = (focus_ >= 0 && focus_ < ItemCount()) ? (*items_)[static_cast<size_t>(focus_)].name : std::wstring();
        const int oldTop = scrollY_ / unit;
        const int frac = scrollY_ % unit;
        std::wstring topName;
        int topIdx = mode_ == ViewMode::Details ? oldTop : oldTop * cols_;
        if (topIdx >= 0 && topIdx < ItemCount()) topName = (*items_)[static_cast<size_t>(topIdx)].name;

        const bool pathChanged = !EqualsNoCase(dir_, tab_->Path());
        items_ = tab_->Entries();
        dir_ = tab_->Path();
        if (pathChanged) {
            sel_.assign(static_cast<size_t>(ItemCount()), 0);
            selCount_ = 0;
            anchor_ = focus_ = -1;
            scrollY_ = 0;
        } else {
            RestoreSelection(names, focusName);
            // 같은 폴더: 맨 위에 보이던 항목을 따라가 스크롤 위치를 유지한다.
            if (!topName.empty()) {
                for (int i = 0; i < ItemCount(); ++i)
                    if ((*items_)[static_cast<size_t>(i)].name == topName) {
                        scrollY_ = rowOf(i) * unit + frac;
                        break;
                    }
            }
        }
        typeBuf_.clear();
        ComputeLayout();
        NotifySelection();
        return;
    }
    if (what & kChangeSort) InvalidateRect(hwnd_, nullptr, FALSE);
    if (what & kChangeMode) NotifySelection();
}

void FileView::ApplyMetrics() { ComputeLayout(); }

// ======================= 그리기 =======================

void FileView::Paint() {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(hwnd_, &ps);
    if (cw_ > 0 && ch_ > 0) {
        if (!memDc_) {
            memDc_ = CreateCompatibleDC(wdc);
            scratchDc_ = CreateCompatibleDC(wdc);
        }
        if (!backBmp_ || backW_ != cw_ || backH_ != ch_) {
            if (backBmp_) {
                SelectObject(memDc_, backOld_);
                DeleteObject(backBmp_);
            }
            backBmp_ = CreateCompatibleBitmap(wdc, cw_, ch_);
            backW_ = cw_;
            backH_ = ch_;
            backOld_ = static_cast<HBITMAP>(SelectObject(memDc_, backBmp_));
        }
        const uint64_t frame = epoch_->fetch_add(1) + 1;
        RECT all{0, 0, cw_, ch_};
        ui::FillRectColor(memDc_, all, ui::kPanelBg);
        SetBkMode(memDc_, TRANSPARENT);
        if (mode_ == ViewMode::Details) PaintDetails(memDc_, frame);
        else PaintTiles(memDc_, frame);
        if (MaxScroll() > 0) {
            RECT tr = ThumbRect();
            RECT track{tr.left, mode_ == ViewMode::Details ? m_.headerH : 0, tr.right, ch_};
            ui::FillRectColor(memDc_, track, ui::kStripBg);
            COLORREF thumbColor = draggingThumb_ ? ui::kText : (ui::IsDarkMode() ? RGB(0x60, 0x60, 0x65) : RGB(0xB8, 0xC0, 0xCC));
            ui::FillRectColor(memDc_, tr, thumbColor);
        }
        BitBlt(wdc, 0, 0, cw_, ch_, memDc_, 0, 0, SRCCOPY);
    }
    EndPaint(hwnd_, &ps);
}

void FileView::DrawSortArrow(HDC dc, int cx, int cy, bool descending) {
    const int s = ui::S(4);
    POINT pts[3];
    if (!descending) {
        pts[0] = {cx - s, cy + s / 2};
        pts[1] = {cx + s, cy + s / 2};
        pts[2] = {cx, cy - s / 2};
    } else {
        pts[0] = {cx - s, cy - s / 2};
        pts[1] = {cx + s, cy - s / 2};
        pts[2] = {cx, cy + s / 2};
    }
    HBRUSH brush = CreateSolidBrush(ui::kTextDim);
    HPEN pen = CreatePen(PS_SOLID, 1, ui::kTextDim);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polygon(dc, pts, 3);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void FileView::PaintDetails(HDC dc, uint64_t frame) {
    const int n = ItemCount();
    int nameW, sizeW, modW;
    ColumnLayout(nameW, sizeW, modW);
    const bool focused = HasFocus();
    const int viewH = ViewHeight(mode_, ch_, m_.headerH);
    ImageService& images = ImageService::Get();
    const UINT textFlags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;

    if (n > 0) {
        const int first = std::max(0, scrollY_ / m_.rowH);
        const int last = std::min(n - 1, (scrollY_ + viewH - 1) / m_.rowH);
        for (int i = first; i <= last; ++i) {
            const FileEntry& e = (*items_)[static_cast<size_t>(i)];
            const int y = m_.headerH + i * m_.rowH - scrollY_;
            RECT row{0, y, cw_, y + m_.rowH};
            const bool selected = static_cast<size_t>(i) < sel_.size() && sel_[static_cast<size_t>(i)] != 0;
            COLORREF bg = selected ? (focused ? ui::kSelectFill : ui::kSelectFillInactive) : ((i & 1) ? ui::kRowAlt : ui::kPanelBg);
            ui::FillRectColor(dc, row, bg);

            ImagePtr img = images.ForEntry(e, dir_, m_.nameIcon, false, epoch_, frame);
            RECT iconCell{ui::S(4), y, ui::S(4) + m_.nameIcon, y + m_.rowH};
            if (img) ImageService::Draw(dc, scratchDc_, *img, iconCell);

            RECT nameRc{iconCell.right + ui::S(8), y, nameW - ui::S(4), y + m_.rowH};
            ui::DrawTextIn(dc, e.name, nameRc, textFlags, ui::kText);
            if (!e.IsDir()) {
                RECT sizeRc{nameW + ui::S(4), y, nameW + sizeW - ui::S(4), y + m_.rowH};
                ui::DrawTextIn(dc, FormatSize(static_cast<int64_t>(e.size)), sizeRc, textFlags, ui::kText);
            }
            RECT modRc{nameW + sizeW + ui::S(4), y, cw_ - ui::S(4), y + m_.rowH};
            ui::DrawTextIn(dc, FormatModified(e.modified), modRc, textFlags, ui::kText);

            if (focused && i == focus_) {
                RECT fr = row;
                InflateRect(&fr, -1, -1);
                ui::FrameRectColor(dc, fr, ui::kSelectBorder);
            }
        }
    }

    // 헤더(스크롤 영역 위에 덮어 그린다)
    RECT header{0, 0, cw_, m_.headerH};
    ui::FillRectColor(dc, header, ui::kHeaderBg);
    RECT line{0, m_.headerH - 1, cw_, m_.headerH};
    ui::FillRectColor(dc, line, ui::kFieldBorder);
    const wchar_t* labels[3] = {L"이름", L"크기", L"수정한 날짜"};
    const int xs[4] = {0, nameW, nameW + sizeW, cw_};
    const SortKey sortKey = tab_ ? tab_->SortColumn() : SortKey::Name;
    const bool desc = tab_ ? tab_->SortDescending() : false;
    for (int c = 0; c < 3; ++c) {
        RECT tr{xs[c] + ui::S(8), 0, xs[c + 1] - ui::S(18), m_.headerH - 1};
        ui::DrawTextIn(dc, labels[c], tr, textFlags, ui::kText);
        if (static_cast<int>(sortKey) == c) DrawSortArrow(dc, xs[c + 1] - ui::S(10), m_.headerH / 2, desc);
        if (c > 0) {
            RECT sep{xs[c], ui::S(4), xs[c] + 1, m_.headerH - ui::S(5)};
            ui::FillRectColor(dc, sep, ui::kFieldBorder);
        }
    }
}

void FileView::PaintTiles(HDC dc, uint64_t frame) {
    const int n = ItemCount();
    if (n == 0) return;
    const bool focused = HasFocus();
    ImageService& images = ImageService::Get();
    const int textH = ui::TextHeight(dc);
    const int gap = ui::S(4);
    const int nominal = ui::S(mode_ == ViewMode::LargeIcons ? 128 : 64);   // WPF 버전: 이름 영역 너비 = 아이콘 크기(DIP)
    const int textW = std::min(nominal, m_.tileW - ui::S(4));
    const int firstRow = std::max(0, scrollY_ / m_.tileH);
    const int lastRow = (scrollY_ + ch_ - 1) / m_.tileH;
    for (int row = firstRow; row <= lastRow; ++row) {
        for (int col = 0; col < cols_; ++col) {
            const int i = row * cols_ + col;
            if (i >= n) break;
            const FileEntry& e = (*items_)[static_cast<size_t>(i)];
            const int x0 = col * m_.tileW;
            const int y0 = row * m_.tileH - scrollY_;
            RECT tile{x0, y0, x0 + m_.tileW, y0 + m_.tileH};
            if (static_cast<size_t>(i) < sel_.size() && sel_[static_cast<size_t>(i)]) {
                RECT sr = tile;
                InflateRect(&sr, -ui::S(2), -ui::S(2));
                ui::FillRectColor(dc, sr, focused ? ui::kSelectFill : ui::kSelectFillInactive);
                ui::FrameRectColor(dc, sr, ui::kSelectBorder);
            }
            const int contentH = m_.iconPx + gap + textH;
            const int top = y0 + (m_.tileH - contentH) / 2;
            RECT iconCell{x0, top, x0 + m_.tileW, top + m_.iconPx};
            ImagePtr img = images.ForEntry(e, dir_, m_.iconPx, true, epoch_, frame);
            if (img) ImageService::Draw(dc, scratchDc_, *img, iconCell);

            RECT textRc{x0 + (m_.tileW - textW) / 2, top + m_.iconPx + gap, x0 + (m_.tileW + textW) / 2, top + m_.iconPx + gap + textH};
            // 이름이 영역에 들어가면 가운데, 넘치면 왼쪽 정렬로 말줄임(WPF 버전과 같은 규칙)
            UINT align = ui::TextWidth(dc, e.name) <= textW ? DT_CENTER : DT_LEFT;
            ui::DrawTextIn(dc, e.name, textRc, align | DT_SINGLELINE | DT_END_ELLIPSIS, ui::kText);

            if (focused && i == focus_) {
                RECT fr = tile;
                InflateRect(&fr, -ui::S(3), -ui::S(3));
                ui::FrameRectColor(dc, fr, ui::kSelectBorder);
            }
        }
    }
}

// ======================= 입력 =======================

void FileView::OnLButtonDown(int x, int y, WPARAM keys) {
    SetFocus(hwnd_);
    if (host_) host_->FvActivate();

    if (MaxScroll() > 0) {
        RECT tr = ThumbRect();
        if (ui::PtIn(tr, x, y)) {
            draggingThumb_ = true;
            dragStartY_ = y;
            dragStartScroll_ = scrollY_;
            SetCapture(hwnd_);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        RECT track{tr.left, mode_ == ViewMode::Details ? m_.headerH : 0, tr.right, ch_};
        if (ui::PtIn(track, x, y)) {
            SetScroll(scrollY_ + (y < tr.top ? -1 : 1) * ViewHeight(mode_, ch_, m_.headerH));
            return;
        }
    }

    if (mode_ == ViewMode::Details && y < m_.headerH) {
        if (int div = HeaderDivider(x)) {
            resizingCol_ = div;
            resizeStartX_ = x;
            resizeStartDip_ = div == 1 ? sizeColDip_ : modColDip_;
            resized_ = false;
        } else {
            headerDown_ = HeaderColumn(x);
        }
        SetCapture(hwnd_);
        return;
    }

    const bool ctrl = (keys & MK_CONTROL) != 0;
    const bool shift = (keys & MK_SHIFT) != 0;
    const int idx = HitTest(x, y);
    collapseTo_ = -1;
    if (idx < 0) {
        if (!ctrl && !shift) {
            ClearSelection();
            NotifySelection();
        }
        return;
    }
    if (shift) {
        if (anchor_ < 0) anchor_ = idx;
        if (!ctrl) ClearSelection();
        SelectRange(anchor_, idx);
    } else if (ctrl) {
        ToggleSelect(idx);
        anchor_ = idx;
    } else if (sel_[static_cast<size_t>(idx)] && selCount_ > 1) {
        collapseTo_ = idx;   // 끌어서 여러 항목을 옮길 수 있게, 버튼을 뗄 때 이 항목만 선택
    } else {
        SelectOnly(idx);
        anchor_ = idx;
    }
    focus_ = idx;
    dragCandidate_ = true;
    downPt_ = {x, y};
    SetCapture(hwnd_);
    NotifySelection();
}

void FileView::OnMouseMove(int x, int y, WPARAM keys) {
    if (draggingThumb_) {
        int vh = ViewHeight(mode_, ch_, m_.headerH);
        int thH = std::min(vh, std::max(ui::S(24), static_cast<int>(static_cast<int64_t>(vh) * vh / std::max(1, ContentHeight()))));
        int trackH = std::max(1, vh - thH);
        int dy = y - dragStartY_;
        SetScroll(dragStartScroll_ + static_cast<int>(static_cast<int64_t>(dy) * MaxScroll() / trackH));
        return;
    }
    if (resizingCol_) {
        int px = ui::S(resizeStartDip_) + (resizeStartX_ - x);
        int dip = std::clamp(MulDiv(px, 96, static_cast<int>(ui::dpi)), 40, 600);
        (resizingCol_ == 1 ? sizeColDip_ : modColDip_) = dip;
        resized_ = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (dragCandidate_ && (keys & MK_LBUTTON)) {
        if (std::abs(x - downPt_.x) >= GetSystemMetrics(SM_CXDRAG) || std::abs(y - downPt_.y) >= GetSystemMetrics(SM_CYDRAG)) {
            dragCandidate_ = false;
            collapseTo_ = -1;
            ReleaseCapture();
            std::vector<std::wstring> paths = SelectedPaths();
            if (!paths.empty()) StartFileDrag(paths);
        }
        return;
    }
    int hot = HitTest(x, y);
    if (hot != hot_) {
        hot_ = hot;
        if (tip_) SendMessageW(tip_, TTM_POP, 0, 0);
    }
}

void FileView::OnLButtonUp(int x, int y) {
    if (GetCapture() == hwnd_) ReleaseCapture();
    if (draggingThumb_) {
        draggingThumb_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (resizingCol_) {
        resizingCol_ = 0;
        return;
    }
    if (headerDown_ >= 0) {
        int col = headerDown_;
        headerDown_ = -1;
        if (y < m_.headerH && HeaderColumn(x) == col && host_) host_->FvSortRequested(static_cast<SortKey>(col));
        return;
    }
    if (collapseTo_ >= 0) {
        SelectOnly(collapseTo_);
        anchor_ = collapseTo_;
        collapseTo_ = -1;
        NotifySelection();
    }
    dragCandidate_ = false;
}

void FileView::OnKeyDown(WPARAM key) {
    const int n = ItemCount();
    if (n == 0) return;
    const bool ctrl = GetKeyState(VK_CONTROL) < 0;
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    const bool tiles = mode_ != ViewMode::Details;
    const int cur = focus_;
    const int page = tiles ? std::max(1, ch_ / m_.tileH) * cols_ : std::max(1, (ch_ - m_.headerH) / m_.rowH);
    int target = -1;
    switch (key) {
        case VK_UP: target = cur < 0 ? 0 : cur - (tiles ? cols_ : 1); break;
        case VK_DOWN:
            target = cur < 0 ? 0 : cur + (tiles ? cols_ : 1);
            if (tiles && cur >= 0 && target >= n) target = (cur / cols_ < (n - 1) / cols_) ? n - 1 : cur;
            break;
        case VK_LEFT: if (tiles) target = cur < 0 ? 0 : cur - 1; break;
        case VK_RIGHT: if (tiles) target = cur < 0 ? 0 : cur + 1; break;
        case VK_HOME: target = 0; break;
        case VK_END: target = n - 1; break;
        case VK_PRIOR: target = tiles ? GridPageTarget(n, cols_, cur, page / cols_, false) : std::max(0, cur - page); break;
        case VK_NEXT: target = tiles ? GridPageTarget(n, cols_, cur, page / cols_, true) : (cur < 0 ? 0 : cur + page); break;
        case 'A':
            if (ctrl) {
                std::fill(sel_.begin(), sel_.end(), static_cast<char>(1));
                selCount_ = n;
                NotifySelection();
            }
            return;
        case VK_SPACE:
            if (ctrl && cur >= 0) {
                ToggleSelect(cur);
                anchor_ = cur;
                NotifySelection();
            }
            return;
        default: return;
    }
    if (target >= 0) MoveFocus(target, shift, ctrl);
}

void FileView::OnChar(wchar_t ch) {
    if (ch < 32 || GetKeyState(VK_CONTROL) < 0) return;
    const int n = ItemCount();
    if (n == 0) return;
    DWORD now = GetTickCount();
    if (now - typeTick_ > 1000) typeBuf_.clear();
    typeTick_ = now;
    if (ch == L' ' && typeBuf_.empty()) return;
    typeBuf_ += ch;
    const int start = std::max(0, focus_) + (typeBuf_.size() == 1 ? 1 : 0);
    for (int k = 0; k < n; ++k) {
        int i = (start + k) % n;
        if (StartsWithNoCase((*items_)[static_cast<size_t>(i)].name, typeBuf_)) {
            MoveFocus(i, false, false);
            return;
        }
    }
}

void FileView::OnWheel(int delta) {
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    int viewH = ViewHeight(mode_, ch_, m_.headerH);
    int amount;
    if (lines == WHEEL_PAGESCROLL) amount = viewH;
    else if (mode_ == ViewMode::Details) amount = m_.rowH * static_cast<int>(lines);
    else amount = std::max(1, m_.tileH * static_cast<int>(lines) / 3);   // 줄 수 3 = 아이콘 한 줄
    SetScroll(scrollY_ - delta * amount / WHEEL_DELTA);
}

void FileView::UpdateTooltipText(NMTTDISPINFOW* info) {
    tipText_ = L"";
    info->lpszText = const_cast<LPWSTR>(L"");
}

LRESULT FileView::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: Paint(); return 0;
        case WM_SIZE: ComputeLayout(); return 0;
        case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_SETFOCUS:
            if (host_) host_->FvActivate();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_KILLFOCUS: InvalidateRect(hwnd_, nullptr, FALSE); return 0;
        case WM_VSCROLL: {
            SCROLLINFO si{};
            si.cbSize = sizeof si;
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd_, SB_VERT, &si);
            const int line = mode_ == ViewMode::Details ? m_.rowH : std::max(1, m_.tileH / 3);
            const int viewH = ViewHeight(mode_, ch_, m_.headerH);
            int pos = scrollY_;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: pos -= line; break;
                case SB_LINEDOWN: pos += line; break;
                case SB_PAGEUP: pos -= viewH; break;
                case SB_PAGEDOWN: pos += viewH; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: pos = si.nTrackPos; break;
                case SB_TOP: pos = 0; break;
                case SB_BOTTOM: pos = MaxScroll(); break;
                default: break;
            }
            SetScroll(pos);
            return 0;
        }
        case WM_MOUSEWHEEL: OnWheel(GET_WHEEL_DELTA_WPARAM(wParam)); return 0;
        case WM_LBUTTONDOWN: OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam); return 0;
        case WM_LBUTTONDBLCLK: {
            int idx = HitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (idx >= 0) {
                if (!sel_[static_cast<size_t>(idx)]) {
                    SelectOnly(idx);
                    focus_ = anchor_ = idx;
                    NotifySelection();
                }
                if (host_) host_->FvOpen();
            } else if (mode_ == ViewMode::Details && GET_Y_LPARAM(lParam) < m_.headerH) {
                OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);   // 빠른 연속 클릭도 정렬/크기 조절로 처리
            }
            return 0;
        }
        case WM_MOUSEMOVE: OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam); return 0;
        case WM_LBUTTONUP: OnLButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lParam) != hwnd_) {
                resizingCol_ = 0;
                headerDown_ = -1;
                draggingThumb_ = false;
            }
            return 0;
        case WM_RBUTTONDOWN: {
            SetFocus(hwnd_);
            if (host_) host_->FvActivate();
            int idx = HitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (idx >= 0 && !sel_[static_cast<size_t>(idx)]) {
                SelectOnly(idx);
                focus_ = anchor_ = idx;
                NotifySelection();
            }
            return 0;
        }
        case WM_CONTEXTMENU: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (lParam == -1) {   // 키보드(Shift+F10, 메뉴 키)
                RECT r = focus_ >= 0 ? ItemRect(focus_) : RECT{0, 0, 0, 0};
                pt = {r.left + ui::S(16), r.top + ui::S(16)};
                ClientToScreen(hwnd_, &pt);
            }
            if (host_) host_->FvContextMenu(pt);
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && mode_ == ViewMode::Details) {
                POINT p;
                GetCursorPos(&p);
                ScreenToClient(hwnd_, &p);
                if (resizingCol_ || (p.y < m_.headerH && HeaderDivider(p.x))) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
            }
            break;
        case WM_KEYDOWN: OnKeyDown(wParam); return 0;
        case WM_CHAR: OnChar(static_cast<wchar_t>(wParam)); return 0;
        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lParam);
            if (hdr && hdr->hwndFrom == tip_ && hdr->code == TTN_GETDISPINFOW) {
                UpdateTooltipText(reinterpret_cast<NMTTDISPINFOW*>(lParam));
                return 0;
            }
            break;
        }
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

}  // namespace ft
