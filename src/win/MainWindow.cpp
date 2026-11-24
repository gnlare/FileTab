#include "MainWindow.h"

#include <algorithm>
#include <cmath>
#include <uxtheme.h>

#include "Dialogs.h"
#include "FsServices.h"
#include "SettingsService.h"
#include "resource.h"

namespace ft {

namespace {
const wchar_t* kMainClass = L"FileTabMainWindow";
enum : int { kIdSettings = 1001, kIdDual = 1002, kIdMode = 1003 };
constexpr UINT_PTR kImageTimer = 2;
constexpr int kMinWidthDip = 850, kMinHeightDip = 450;

RECT BarRect(int clientW) {
    int m = ui::S(10);
    return {m, m, std::max(m, clientW - m), m + ui::S(40)};
}
}  // namespace

// ======================= 생성 =======================

MainWindow* MainWindow::Create(HINSTANCE instance, int showCommand) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = &MainWindow::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kMainClass;
    if (!RegisterClassExW(&wc)) return nullptr;
    if (!FileView::RegisterWindowClass(instance) || !PaneView::RegisterWindowClass(instance)) return nullptr;

    ui::SetDpi(GetDpiForSystem());
    const AppSettings& st = SettingsService::Get().state;
    auto dipToPx = [](double dip) { return static_cast<int>(dip * ui::dpi / 96.0 + 0.5); };
    double widthDip = 1280, heightDip = 820;
    if (st.dualPane && st.dualWindowWidth && *st.dualWindowWidth >= kMinWidthDip) widthDip = *st.dualWindowWidth;
    else if (st.windowWidth && *st.windowWidth >= kMinWidthDip) widthDip = *st.windowWidth;
    if (st.windowHeight && *st.windowHeight >= kMinHeightDip) heightDip = *st.windowHeight;
    int w = std::min(dipToPx(widthDip), GetSystemMetrics(SM_CXVIRTUALSCREEN));
    int h = std::min(dipToPx(heightDip), GetSystemMetrics(SM_CYVIRTUALSCREEN));
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + (ui::RcW(work) - w) / 2, y = work.top + (ui::RcH(work) - h) / 2;

    auto* self = new MainWindow();
    HWND hwnd = CreateWindowExW(0, kMainClass, L"FileTab", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, w, h, nullptr, nullptr, instance, self);
    if (!hwnd) {
        delete self;
        return nullptr;
    }
    UINT actual = GetDpiForWindow(hwnd);
    if (actual && actual != ui::dpi) {   // 시작 모니터의 배율이 시스템 배율과 다르면 글꼴·배치를 맞춘다
        ui::SetDpi(actual);
        self->ApplyFonts();
        self->left_->ApplyDpi();
        self->right_->ApplyDpi();
        self->LayoutAll();
    }
    ShowWindow(hwnd, st.isMaximized ? SW_SHOWMAXIMIZED : showCommand);
    UpdateWindow(hwnd);
    if (st.darkMode) {
        ui::SetDarkMode(true);
        self->ApplyTheme();
    }
    self->left_->FocusFiles();
    return self;
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self;
    if (msg == WM_NCCREATE) {
        self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->hwnd_ = hwnd;
        g_mainWnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->HandleMessage(msg, wParam, lParam);
}

void MainWindow::CreateChildren() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    whiteBrush_ = CreateSolidBrush(ui::kPanelBg);
    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
        return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
    };
    settingsBtn_ = make(L"BUTTON", L"설정", WS_TABSTOP | BS_OWNERDRAW, kIdSettings);
    dualBtn_ = make(L"BUTTON", L"좌우 분할", WS_TABSTOP | BS_OWNERDRAW, kIdDual);
    modeLabel_ = make(L"STATIC", L"보기", SS_LEFT, -1);
    modeCombo_ = make(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS, kIdMode);
    SendMessageW(modeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"자세히"));
    SendMessageW(modeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"큰 아이콘 · 128px"));
    SendMessageW(modeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"보통 아이콘 · 64px"));
    SendMessageW(modeCombo_, CB_SETCURSEL, 0, 0);
    ApplyFonts();

    left_.reset(PaneView::Create(hwnd_));
    right_.reset(PaneView::Create(hwnd_));
    SearchBar::RegisterWindowClass(inst);
    SearchBar::Callbacks scb;
    scb.onSearch = [this](const TabSearchState& st) {
        if (auto* p = ActivePane()) p->ExecuteSearch(st);
    };
    scb.onCancel = [this]() {
        if (auto* p = ActivePane()) p->CancelSearch();
    };
    searchBar_.reset(SearchBar::Create(hwnd_, std::move(scb)));

    for (PaneView* pane : {left_.get(), right_.get()}) {
        pane->cb.activated = [this](PaneView* p) { SetActive(p); };
        pane->cb.action = [this](PaneView* p, FileAction a) { RunAction(p, a); };
        pane->cb.modeChanged = [this]() { SyncMode(); };
        pane->cb.bookmarksChanged = [this]() {
            left_->OnBookmarksChanged();
            right_->OnBookmarksChanged();
        };
        pane->cb.searchChanged = [this]() { SyncSearch(); };
    }
    ImageService::Get().Init(hwnd_);

    const AppSettings& st = SettingsService::Get().state;
    if (st.leftPaneWidth && st.rightPaneWidth && *st.leftPaneWidth > 0 && *st.rightPaneWidth > 0) {
        const double total = *st.leftPaneWidth + *st.rightPaneWidth;
        if (std::isfinite(total) && total > 0)
            leftFrac_ = std::clamp(*st.leftPaneWidth / total, 0.05, 0.95);
    }
    left_->Restore(st.left);
    right_->Restore(st.right);
    SetActive(left_.get());
    LayoutAll();
    left_->SetPaneVisible(true);
    SetDual(st.dualPane);
    if (!SettingsService::Get().loadWarning.empty()) ui::ShowError(hwnd_, SettingsService::Get().loadWarning, L"설정");
}

void MainWindow::ApplyFonts() {
    if (titleLabel_) SendMessageW(titleLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(ui::fontBold), TRUE);
    for (HWND h : {settingsBtn_, dualBtn_, modeLabel_, modeCombo_})
        if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(ui::font), TRUE);
    if (searchBar_) searchBar_->ApplyDpi();
}

// ======================= 배치 =======================

RECT MainWindow::PaneArea() const {
    RECT cr{};
    GetClientRect(hwnd_, &cr);
    RECT bar = BarRect(cr.right);
    int m = ui::S(10);
    const int W = cr.right, H = cr.bottom;
    const int top = bar.bottom + ui::S(8);
    return {m, top, std::max(m, W - m), std::max(top, H - m)};
}

RECT MainWindow::SplitterRect() const {
    RECT ar = PaneArea();
    if (!dual_) return {0, 0, 0, 0};
    const int split = ui::S(6), avail = ui::RcW(ar) - split, minW = ui::S(300);
    int lw = static_cast<int>(avail * leftFrac_);
    lw = avail < 2 * minW ? avail / 2 : std::clamp(lw, minW, avail - minW);
    return {ar.left + lw, ar.top, ar.left + lw + split, ar.bottom};
}

void MainWindow::LayoutAll() {
    if (!left_ || !right_) return;
    RECT cr{};
    GetClientRect(hwnd_, &cr);
    RECT bar = BarRect(cr.right);
    const int barH = ui::RcH(bar);
    int x = bar.left + ui::S(14);
    auto centered = [&](int h) { return bar.top + (barH - h) / 2; };
    if (titleLabel_) MoveWindow(titleLabel_, 0, 0, 0, 0, FALSE);

    HDC dc = GetDC(hwnd_);
    int setW = ui::S(20) + ui::TextWidth(dc, L"설정", ui::font);
    const wchar_t* dTxt = dual_ ? L"단일 창" : L"좌우 분할";
    int dualW = ui::S(20) + ui::TextWidth(dc, dTxt, ui::font);
    int comboW = ui::S(34) + ui::TextWidth(dc, L"보통 아이콘 · 64px", ui::font);
    ReleaseDC(hwnd_, dc);

    MoveWindow(settingsBtn_, x, centered(ui::S(26)), setW, ui::S(26), TRUE);
    x += setW + ui::S(8);
    MoveWindow(dualBtn_, x, centered(ui::S(26)), dualW, ui::S(26), TRUE);
    x += dualW + ui::S(16);
    MoveWindow(modeLabel_, x, centered(ui::S(18)), ui::S(32), ui::S(18), TRUE);
    x += ui::S(36);
    MoveWindow(modeCombo_, x, centered(ui::S(26)), comboW, ui::S(26 + 150), TRUE);   // 높이에는 펼침 목록이 포함된다

    int searchW = ui::S(260);
    int searchH = ui::S(26);
    int searchX = bar.right - ui::S(14) - searchW;
    if (searchBar_) searchBar_->SetBounds(searchX, centered(searchH), searchW, searchH);

    RECT ar = PaneArea();
    if (!dual_) {
        MoveWindow(left_->hwnd(), ar.left, ar.top, ui::RcW(ar), ui::RcH(ar), TRUE);
    } else {
        RECT sp = SplitterRect();
        MoveWindow(left_->hwnd(), ar.left, ar.top, sp.left - ar.left, ui::RcH(ar), TRUE);
        MoveWindow(right_->hwnd(), sp.right, ar.top, ar.right - sp.right, ui::RcH(ar), TRUE);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::SetDual(bool enabled) {
    dual_ = enabled;
    SetWindowTextW(dualBtn_, enabled ? L"단일 창" : L"좌우 분할");
    LayoutAll();
    right_->SetPaneVisible(enabled);
    if (enabled) {
        SetActive(right_.get());
        right_->FocusFiles();
    } else if (active_ == right_.get()) {
        SetActive(left_.get());
        left_->FocusFiles();
    }
}

void MainWindow::ApplyTheme() {
    ui::ApplyWindowTheme(hwnd_);
    if (whiteBrush_) DeleteObject(whiteBrush_);
    whiteBrush_ = CreateSolidBrush(ui::kPanelBg);
    if (left_) left_->ApplyTheme();
    if (right_) right_->ApplyTheme();
    if (searchBar_) searchBar_->ApplyTheme();
    SetWindowTheme(modeCombo_, ui::IsDarkMode() ? L"DarkMode_CFD" : L"Explorer", nullptr);
    COMBOBOXINFO ci{}; ci.cbSize = sizeof(ci);
    if (GetComboBoxInfo(modeCombo_, &ci)) {
        SetWindowTheme(ci.hwndList, ui::IsDarkMode() ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        RedrawWindow(ci.hwndList, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE);
    }
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void MainWindow::SetActive(PaneView* pane) {
    if (pane == right_.get() && !dual_) return;
    active_ = pane;
    left_->SetActive(pane == left_.get());
    right_->SetActive(pane == right_.get());
    SyncMode();
    SyncSearch();
}

void MainWindow::SyncSearch() {
    if (!searchBar_) return;
    if (auto* pane = ActivePane()) {
        searchBar_->SetState(pane->GetCurrentSearchState());
    }
}

void MainWindow::SyncMode() {
    if (!modeCombo_ || !active_) return;
    auto tab = active_->SelectedTab();
    syncing_ = true;
    SendMessageW(modeCombo_, CB_SETCURSEL, tab ? static_cast<WPARAM>(tab->Mode()) : 0, 0);
    syncing_ = false;
}

PaneView* MainWindow::PaneFromWindow(HWND window) const {
    if (!window) return nullptr;
    for (PaneView* pane : {left_.get(), right_.get()})
        if (pane && (window == pane->hwnd() || IsChild(pane->hwnd(), window))) return pane;
    return nullptr;
}

// ======================= 파일 작업 =======================

void MainWindow::RunAction(PaneView* pane, FileAction action) {
    if (busy_ || !pane) return;
    // 동작 시작 시점의 원본·대상을 먼저 확보한다(이후 포커스·이동이 바뀌어도 대상이 바뀌지 않는다).
    auto selected = pane->SelectedEntries();
    auto tab = pane->SelectedTab();
    const std::wstring destination = tab ? tab->Path() : std::wstring();
    std::vector<std::wstring> paths = pane->SelectedPaths();

    if (action == FileAction::Open) {
        pane->OpenSelected();
        return;
    }
    if (action == FileAction::Copy) {
        if (!paths.empty()) CopyFilesToClipboard(hwnd_, paths);
        return;
    }
    std::wstring newName;
    if (action == FileAction::Paste) {
        if (destination.empty() || !ClipboardHasFiles()) return;
        paths = ReadClipboardFiles(hwnd_);
        if (paths.empty()) return;
    } else if (action == FileAction::Delete || action == FileAction::PermanentDelete) {
        if (paths.empty()) return;
        std::wstring preview;
        for (size_t i = 0; i < selected.size() && i < 5; ++i) preview += (i ? L"\n" : L"") + selected[i].name;
        std::wstring count = FormatThousands(static_cast<int64_t>(paths.size()));
        std::wstring text;
        const wchar_t* title;
        if (action == FileAction::PermanentDelete) {
            text = L"선택한 " + count + L"개 항목을 완전히 삭제하시겠습니까?\n\n" + preview +
                   L"\n\n이 작업은 휴지통을 거치지 않고 영구적으로 삭제되며 되돌릴 수 없습니다.";
            title = L"영구 삭제 확인";
        } else {
            text = L"선택한 " + count + L"개 항목을 삭제하시겠습니까?\n\n" + preview + L"\n\n휴지통을 우선 사용합니다. 사용할 수 없는 위치는 Windows에서 추가 확인합니다.";
            title = L"삭제 확인";
        }
        if (MessageBoxW(hwnd_, text.c_str(), title, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
    } else if (action == FileAction::Rename) {
        if (selected.size() != 1) return;
        const FileEntry& e = selected[0];
        newName = e.name;
        int selEnd = e.IsDir() ? static_cast<int>(e.name.size()) : static_cast<int>(path::StemOf(e.name).size());
        NameValidator validate = [](const std::wstring& n) { return path::ValidateName(n); };
        if (!InputDialog(hwnd_, L"이름 바꾸기", L"새 이름 (확장자 포함)", newName, 0, selEnd, L"변경", validate)) return;
        if (newName == e.name) return;
    }

    busy_ = true;
    switch (action) {
        case FileAction::Paste: RunCopy(paths, destination, hwnd_); break;
        case FileAction::Delete: RunDelete(paths, hwnd_, true); break;
        case FileAction::PermanentDelete: RunDelete(paths, hwnd_, false); break;
        case FileAction::Rename: RunRename(paths[0], newName, hwnd_); break;
        default: busy_ = false; break;
    }
}

void MainWindow::OnOperationDone(OpResult* raw) {
    std::unique_ptr<OpResult> result(raw);
    busy_ = false;
    if (!result->ok && !result->error.empty()) ui::ShowError(hwnd_, result->error, L"파일 작업 실패");
    RefreshVisible();
}

void MainWindow::RefreshVisible() {
    left_->Refresh();
    if (dual_) right_->Refresh();
}

// ======================= 종료 / 저장 =======================

void MainWindow::SaveState() {
    AppSettings& s = SettingsService::Get().state;
    s.left = left_->Capture();
    s.right = right_->Capture();
    s.dualPane = dual_;
    WINDOWPLACEMENT wp{};
    wp.length = sizeof wp;
    if (GetWindowPlacement(hwnd_, &wp)) {
        const double width = (wp.rcNormalPosition.right - wp.rcNormalPosition.left) * 96.0 / ui::dpi;
        const double height = (wp.rcNormalPosition.bottom - wp.rcNormalPosition.top) * 96.0 / ui::dpi;
        s.isMaximized = wp.showCmd == SW_SHOWMAXIMIZED;
        if (dual_) {
            s.dualWindowWidth = width;
            s.leftPaneWidth = leftFrac_ * 1000.0;
            s.rightPaneWidth = (1.0 - leftFrac_) * 1000.0;
        } else {
            s.windowWidth = width;
        }
        s.windowHeight = height;
    }
    std::wstring error;
    if (!SettingsService::Get().Save(&error)) ui::ShowError(hwnd_, L"설정을 저장하지 못했습니다: " + error);
}

bool MainWindow::OnClose() {
    if (busy_) {
        MessageBoxW(hwnd_, L"진행 중인 파일 작업을 완료하거나 취소한 뒤 종료하세요.", L"종료 확인", MB_OK | MB_ICONWARNING);
        return false;
    }
    SaveState();
    left_->Dispose();
    right_->Dispose();
    return true;
}

// ======================= 키보드 =======================

bool MainWindow::PreTranslate(const MSG& m) {
    if (m.message != WM_KEYDOWN && m.message != WM_SYSKEYDOWN) return false;
    if (!hwnd_ || (m.hwnd != hwnd_ && !IsChild(hwnd_, m.hwnd))) return false;   // 대화상자 등 다른 창의 키는 건드리지 않는다
    if (m.message == WM_KEYDOWN && ((left_ && left_->HandleFindKey(m.wParam)) || (right_ && right_->HandleFindKey(m.wParam)))) return true;
    return HandleKey(m.wParam);
}

bool MainWindow::HandleKey(WPARAM key) {
    const bool ctrl = GetKeyState(VK_CONTROL) < 0, alt = GetKeyState(VK_MENU) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    const bool none = !ctrl && !alt && !shift;
    PaneView* pane = ActivePane();
    auto tab = pane->SelectedTab();

    auto matches = [&](const std::string& id, const std::wstring& defShortcut) {
        if (IsFixedShortcutKey(static_cast<unsigned int>(key), ctrl, alt, shift))
            return MatchShortcut(defShortcut, static_cast<unsigned int>(key), ctrl, alt, shift);
        std::wstring sc = defShortcut;
        for (const auto& item : SettingsService::Get().state.shortcuts) {
            if (item.id == id) return MatchShortcut(item.shortcut, static_cast<unsigned int>(key), ctrl, alt, shift) ||
                MatchShortcut(item.shortcut2, static_cast<unsigned int>(key), ctrl, alt, shift);
        }
        return MatchShortcut(sc, static_cast<unsigned int>(key), ctrl, alt, shift);
    };

    if (matches("dual_pane", L"F6")) {
        SetDual(!dual_);
        return true;
    }
    if (matches("new_tab", L"Ctrl+T")) {
        pane->NewTab();
        return true;
    }
    if (matches("close_tab", L"Ctrl+W")) {
        pane->CloseSelectedTab();
        return true;
    }
    if (matches("address", L"F4")) {
        pane->EditAddress();
        return true;
    }
    if (matches("find", L"Ctrl+F")) {
        if (searchBar_) searchBar_->Focus();
        return true;
    }
    
    if (ctrl && !alt && key == VK_TAB) { pane->SelectNextTab(shift ? -1 : 1); return true; }
    if (matches("back", L"Alt+Left")) {
        if (tab) tab->Back();
        return true;
    }
    if (matches("forward", L"Alt+Right")) {
        if (tab) tab->Forward();
        return true;
    }
    if ((key != VK_BACK || left_->IsFileViewFocused() || right_->IsFileViewFocused()) && matches("parent", L"Alt+Up")) {
        pane->Up();
        return true;
    }
    if (MatchShortcut(L"F5", static_cast<unsigned int>(key), ctrl, alt, shift)) {
        pane->Refresh();
        return true;
    }
    if (matches("bookmark", L"Ctrl+D")) {
        pane->AddCurrentBookmark();
        return true;
    }

    // 아래 키는 파일 목록이 포커스를 가진 때만 처리한다(주소 입력 칸의 글 편집 등을 방해하지 않기 위함).
    if (!(left_->IsFileViewFocused() || right_->IsFileViewFocused())) return false;
    FileAction action;
    if (MatchShortcut(L"Ctrl+C", static_cast<unsigned int>(key), ctrl, alt, shift)) action = FileAction::Copy;
    else if (MatchShortcut(L"Ctrl+V", static_cast<unsigned int>(key), ctrl, alt, shift)) action = FileAction::Paste;
    else if (MatchShortcut(L"Del", static_cast<unsigned int>(key), ctrl, alt, shift)) action = FileAction::Delete;
    else if (MatchShortcut(L"Shift+Del", static_cast<unsigned int>(key), ctrl, alt, shift)) action = FileAction::PermanentDelete;
    else if (MatchShortcut(L"F2", static_cast<unsigned int>(key), ctrl, alt, shift)) action = FileAction::Rename;
    else if (key == VK_RETURN && none) action = FileAction::Open;
    else return false;
    RunAction(pane, action);
    return true;
}

// ======================= 창 프로시저 =======================

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: CreateChildren(); return 0;
        case WM_MEASUREITEM: {
            auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (mi && mi->CtlID == kIdMode) { mi->itemHeight = ui::S(22); return TRUE; }
            break;
        }
        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (dis && dis->CtlID == kIdMode) {
                const bool selected = (dis->itemState & ODS_SELECTED) != 0;
                ui::FillRectColor(dis->hDC, dis->rcItem, selected ? ui::kSelectFill : ui::kFieldBg);
                if (dis->itemID != static_cast<UINT>(-1)) {
                    wchar_t text[128]{};
                    SendMessageW(dis->hwndItem, CB_GETLBTEXT, dis->itemID, reinterpret_cast<LPARAM>(text));
                    RECT rc = dis->rcItem; rc.left += ui::S(6);
                    ui::DrawTextIn(dis->hDC, text, rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE, ui::kText, ui::font);
                }
                return TRUE;
            }
            if (dis && (dis->CtlID == kIdSettings || dis->CtlID == kIdDual)) {
                HDC dc = dis->hDC;
                RECT rc = dis->rcItem;
                bool isPressed = (dis->itemState & ODS_SELECTED) != 0;
                COLORREF bg = isPressed ? ui::kPressed : ui::kButtonBg;
                ui::FillRectColor(dc, rc, bg);
                ui::FrameRectColor(dc, rc, ui::kButtonBorder);
                wchar_t text[64]{};
                GetWindowTextW(dis->hwndItem, text, 64);
                ui::DrawTextIn(dc, text, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE, ui::kText, ui::font);
                return TRUE;
            }
            break;
        }
        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            RECT cr{};
            GetClientRect(hwnd_, &cr);
            ui::FillRectColor(dc, cr, ui::kWindowBg);
            RECT bar = BarRect(cr.right);
            ui::FillRectColor(dc, bar, ui::kPanelBg);
            return 1;
        }
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, ui::kPanelBg);
            SetTextColor(dc, ui::kText);
            return reinterpret_cast<LRESULT>(whiteBrush_);
        }
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) LayoutAll();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = ui::S(kMinWidthDip);
            info->ptMinTrackSize.y = ui::S(kMinHeightDip);
            return 0;
        }
        case WM_DPICHANGED: {
            ui::SetDpi(HIWORD(wParam));
            ApplyFonts();
            if (left_) left_->ApplyDpi();
            if (right_) right_->ApplyDpi();
            const RECT* r = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            LayoutAll();
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == kIdSettings) {
                auto tab = ActivePane()->SelectedTab();
                if (SettingsDialog(hwnd_, tab ? tab->Mode() : ViewMode::LargeIcons)) {
                    ApplyTheme();
                    left_->ApplyDpi();    // 아이콘 여백 및 테마가 바뀌었으므로 배치를 다시 계산
                    right_->ApplyDpi();
                    LayoutAll();
                }
                ActivePane()->FocusFiles();
            } else if (id == kIdDual) {
                SetDual(!dual_);
            } else if (id == kIdMode && HIWORD(wParam) == CBN_SELCHANGE && !syncing_) {
                int index = static_cast<int>(SendMessageW(modeCombo_, CB_GETCURSEL, 0, 0));
                if (auto tab = ActivePane()->SelectedTab(); tab && index >= 0) tab->SetMode(ClampViewMode(index));
                ActivePane()->FocusFiles();
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
            if (dual_ && ui::PtIn(SplitterRect(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) {
                draggingSplitter_ = true;
                SetCapture(hwnd_);
            }
            return 0;
        case WM_MOUSEMOVE:
            if (draggingSplitter_) {
                RECT ar = PaneArea();
                const int split = ui::S(6), avail = ui::RcW(ar) - split;
                if (avail > 0) {
                    leftFrac_ = std::clamp(static_cast<double>(GET_X_LPARAM(lParam) - ar.left - split / 2) / avail, 0.05, 0.95);
                    LayoutAll();
                }
            }
            return 0;
        case WM_LBUTTONUP:
            if (draggingSplitter_) {
                draggingSplitter_ = false;
                ReleaseCapture();
            }
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && dual_) {
                POINT p;
                GetCursorPos(&p);
                ScreenToClient(hwnd_, &p);
                if (draggingSplitter_ || ui::PtIn(SplitterRect(), p.x, p.y)) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
            }
            break;
        case WM_APPCOMMAND: {   // 마우스 뒤로/앞으로 버튼(DefWindowProc 이 WM_XBUTTON 을 변환해서 보낸다)
            const int cmd = GET_APPCOMMAND_LPARAM(lParam);
            if (cmd == APPCOMMAND_BROWSER_BACKWARD || cmd == APPCOMMAND_BROWSER_FORWARD) {
                POINT p;
                GetCursorPos(&p);
                PaneView* pane = PaneFromWindow(WindowFromPoint(p));
                if (!pane || (pane == right_.get() && !dual_)) pane = ActivePane();
                if (auto tab = pane->SelectedTab()) {
                    if (cmd == APPCOMMAND_BROWSER_BACKWARD) tab->Back();
                    else tab->Forward();
                }
                return TRUE;
            }
            break;
        }
        case WM_FT_DIRREAD: DispatchDirResult(reinterpret_cast<DirResult*>(lParam)); return 0;
        case WM_FT_FSCHANGE: {
            PaneView* pane = PaneFromWindow(reinterpret_cast<HWND>(lParam));
            if (pane) pane->OnFsChange(static_cast<uintptr_t>(wParam));
            return 0;
        }
        case WM_FT_IMAGES:
            ImageService::Get().AckNotify();
            if (!imageTimer_) imageTimer_ = SetTimer(hwnd_, kImageTimer, 40, nullptr);   // 완료 알림을 묶어서 한 번에 다시 그린다
            return 0;
        case WM_TIMER:
            if (wParam == kImageTimer) {
                KillTimer(hwnd_, kImageTimer);
                imageTimer_ = 0;
                if (left_) left_->RepaintFiles();
                if (right_ && dual_) right_->RepaintFiles();
                return 0;
            }
            break;
        case WM_FT_OPDONE: OnOperationDone(reinterpret_cast<OpResult*>(lParam)); return 0;
        case WM_CLOSE:
            if (OnClose()) DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

}  // namespace ft
