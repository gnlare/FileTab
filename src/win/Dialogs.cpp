#include "Dialogs.h"

#include <algorithm>

#include "FsServices.h"
#include "MainWindow.h"
#include "SettingsService.h"

namespace ft {

namespace {

const wchar_t* kDlgClass = L"FileTabDialog";
enum : int {
    kIdEdit1 = 101, kIdEdit2 = 102, kIdList = 103, kIdAdd = 104, kIdEdit = 105,
    kIdRemove = 106, kIdUp = 107, kIdDown = 108, kIdCategoryVisual = 109,
    kIdChkDarkMode = 110, kIdCategoryShortcuts = 111,
    kIdShortcutList = 112, kIdBtnChangeShortcut = 113, kIdBtnResetShortcut = 114
};

HWND MakeControl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, ui::S(x), ui::S(y), ui::S(w), ui::S(h), parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(ui::font), TRUE);
    return c;
}
HWND MakeLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h) { return MakeControl(parent, L"STATIC", text, SS_LEFT, x, y, w, h, -1); }
HWND MakeEdit(HWND parent, const std::wstring& text, int x, int y, int w, int h, int id, DWORD extra = 0) {
    HWND e = MakeControl(parent, L"EDIT", text.c_str(), WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | extra, x, y, w, h, id);
    return e;
}
HWND MakeButton(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id, bool isDefault = false) {
    return MakeControl(parent, L"BUTTON", text, WS_TABSTOP | (isDefault ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON), x, y, w, h, id);
}

// 코드로 만드는 모달 대화상자의 공통 틀
class Dialog {
public:
    virtual ~Dialog() = default;

    bool Run(HWND owner, const wchar_t* title, int widthDip, int heightDip) {
        owner_ = owner;
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof wc;
            wc.lpfnWndProc = &Dialog::Proc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
            wc.lpszClassName = kDlgClass;
            RegisterClassExW(&wc);
            registered = true;
        }
        const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
        RECT rc{0, 0, ui::S(widthDip), ui::S(heightDip)};
        AdjustWindowRectEx(&rc, style, FALSE, WS_EX_DLGMODALFRAME);
        RECT orc{};
        if (!owner || !GetWindowRect(owner, &orc)) orc = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        const int w = rc.right - rc.left, h = rc.bottom - rc.top;
        const int x = orc.left + (ui::RcW(orc) - w) / 2, y = orc.top + (ui::RcH(orc) - h) / 2;
        hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kDlgClass, title, style, x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr), this);
        if (!hwnd_) return false;
        ui::ApplyWindowTheme(hwnd_);
        if (owner) EnableWindow(owner, FALSE);
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        OnShown();
        MSG msg;
        while (!done_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (FilterMessage(msg)) continue;
            if (!IsDialogMessageW(hwnd_, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        if (!done_) PostQuitMessage(0);   // 앱 종료 요청이면 바깥 루프도 끝내게 한다
        if (owner) EnableWindow(owner, TRUE);
        DestroyWindow(hwnd_);
        if (owner) SetForegroundWindow(owner);
        return accepted_;
    }

protected:
    virtual void OnCreate() = 0;
    virtual void OnShown() {}
    virtual bool FilterMessage(const MSG&) { return false; }
    virtual bool OnOk() = 0;                  // true 면 승인하고 닫는다
    virtual void OnCommand(int) {}
    virtual LRESULT OnMessage(UINT /*msg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, bool& handled) {
        handled = false;
        return 0;
    }
    void Finish(bool ok) {
        accepted_ = ok;
        done_ = true;
    }
    void Warn(const wchar_t* message, const wchar_t* title) { MessageBoxW(hwnd_, message, title, MB_OK | MB_ICONWARNING); }
    HWND hwnd_ = nullptr;
    HWND owner_ = nullptr;

private:
    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        Dialog* self;
        if (msg == WM_NCCREATE) {
            self = static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<Dialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
        bool handled = false;
        LRESULT res = self->OnMessage(msg, wParam, lParam, handled);
        if (handled) return res;
        switch (msg) {
            case WM_CREATE: self->OnCreate(); return 0;
            case WM_COMMAND: {
                int id = LOWORD(wParam);
                if (id == IDOK) {
                    if (self->OnOk()) self->Finish(true);
                } else if (id == IDCANCEL) {
                    self->Finish(false);
                } else {
                    self->OnCommand(id);
                }
                return 0;
            }
            case WM_CLOSE: self->Finish(false); return 0;
            default: break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    bool done_ = false;
    bool accepted_ = false;
};

// ---------- 이름 입력 ----------
class InputDlg final : public Dialog {
public:
    InputDlg(const wchar_t* prompt, std::wstring& text, int selStart, int selEnd, const wchar_t* okLabel, const NameValidator& validate)
        : prompt_(prompt), text_(text), selStart_(selStart), selEnd_(selEnd), okLabel_(okLabel), validate_(validate) {}

protected:
    void OnCreate() override {
        MakeLabel(hwnd_, prompt_, 14, 12, 392, 36);
        edit_ = MakeEdit(hwnd_, text_, 14, 52, 392, 24, kIdEdit1);
        MakeButton(hwnd_, okLabel_, 252, 98, 72, 26, IDOK, true);
        MakeButton(hwnd_, L"취소", 332, 98, 72, 26, IDCANCEL);
    }
    void OnShown() override {
        SetFocus(edit_);
        SendMessageW(edit_, EM_SETSEL, selStart_, selEnd_);
    }
    bool OnOk() override {
        std::wstring value = ui::GetText(edit_);
        if (validate_) {
            std::wstring error = validate_(value);
            if (!error.empty()) {
                Warn(error.c_str(), L"이름 확인");
                SetFocus(edit_);
                return false;
            }
        }
        text_ = value;
        return true;
    }

private:
    const wchar_t* prompt_;
    std::wstring& text_;
    int selStart_, selEnd_;
    const wchar_t* okLabel_;
    const NameValidator& validate_;
    HWND edit_ = nullptr;
};

// ---------- 북마크 수정 ----------
class BookmarkDlg final : public Dialog {
public:
    explicit BookmarkDlg(Bookmark& b) : b_(b) {}

protected:
    void OnCreate() override {
        MakeLabel(hwnd_, L"이름", 14, 12, 400, 18);
        name_ = MakeEdit(hwnd_, b_.name, 14, 32, 412, 24, kIdEdit1);
        MakeLabel(hwnd_, L"경로", 14, 66, 400, 18);
        path_ = MakeEdit(hwnd_, b_.path, 14, 86, 412, 24, kIdEdit2);
        MakeButton(hwnd_, L"확인", 270, 130, 72, 26, IDOK, true);
        MakeButton(hwnd_, L"취소", 350, 130, 72, 26, IDCANCEL);
    }
    void OnShown() override {
        SetFocus(name_);
        SendMessageW(name_, EM_SETSEL, 0, -1);
    }
    bool OnOk() override {
        std::wstring name = ui::GetText(name_), pathText = ui::GetText(path_);
        if (Trim(name).empty()) {
            Warn(L"북마크 이름을 입력하세요.", L"북마크 수정");
            SetFocus(name_);
            return false;
        }
        if (Trim(pathText).empty()) {
            Warn(L"경로를 입력하세요.", L"북마크 수정");
            SetFocus(path_);
            return false;
        }
        std::wstring normalized, error;
        if (!path::TryNormalize(pathText, L"", normalized, &error)) {
            Warn((L"올바르지 않은 경로 형식입니다: " + error).c_str(), L"북마크 수정");
            SetFocus(path_);
            return false;
        }
        if (!IsDirectoryPath(normalized)) {
            Warn(L"존재하지 않는 폴더 경로입니다.", L"북마크 수정");
            SetFocus(path_);
            return false;
        }
        b_.name = Trim(name);
        b_.path = normalized;
        return true;
    }

private:
    Bookmark& b_;
    HWND name_ = nullptr, path_ = nullptr;
};

// ---------- 설정(카테고리: 비주얼, 단축키) ----------
class SettingsDlg final : public Dialog {
public:
    explicit SettingsDlg(ViewMode current)
        : current_(current),
          target_(current == ViewMode::MediumIcons ? ViewMode::MediumIcons : ViewMode::LargeIcons) {}

    ~SettingsDlg() override {
        if (panelBrush_) DeleteObject(panelBrush_);
    }

protected:
    enum class Category { Visual, Shortcuts };

    void OnCreate() override {
        panelBrush_ = CreateSolidBrush(ui::kPanelBg);
        origDarkMode_ = SettingsService::Get().state.darkMode;
        darkChecked_ = origDarkMode_;
        workingShortcuts_ = SettingsService::Get().state.shortcuts;
        if (workingShortcuts_.empty()) workingShortcuts_ = DefaultShortcuts();

        // 왼쪽: 카테고리 버튼 "비주얼", "단축키" (소유자 그리기)
        catVisual_ = MakeControl(hwnd_, L"BUTTON", L"비주얼", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 8, 8, 114, 34, kIdCategoryVisual);
        catShortcuts_ = MakeControl(hwnd_, L"BUTTON", L"단축키", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 8, 46, 114, 34, kIdCategoryShortcuts);

        // --- 비주얼 카테고리 ---
        // 소제목 "테마"
        titleTheme_ = MakeLabel(hwnd_, L"테마", 150, 16, 42, 22);
        SendMessageW(titleTheme_, WM_SETFONT, reinterpret_cast<WPARAM>(ui::fontBold), TRUE);

        // "다크 모드" 체크박스 (소유자 그리기)
        chkDarkMode_ = MakeControl(hwnd_, L"BUTTON", L"다크 모드", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 154, 44, 140, 24, kIdChkDarkMode);

        // 소제목 "아이콘"
        titleIcon_ = MakeLabel(hwnd_, L"아이콘", 150, 80, 52, 22);
        SendMessageW(titleIcon_, WM_SETFONT, reinterpret_cast<WPARAM>(ui::fontBold), TRUE);

        // 1줄 배치 -> "여백" [vert_] x [horiz_] px
        const IconMargins& m = SettingsService::Get().Margins(target_);
        lblMargin_ = MakeLabel(hwnd_, L"여백", 154, 114, 38, 20);
        vert_ = MakeEdit(hwnd_, std::to_wstring(m.vertical), 196, 110, 60, 26, kIdEdit1, ES_NUMBER | ES_CENTER);
        lblX_ = MakeLabel(hwnd_, L"x", 262, 114, 16, 20);
        horiz_ = MakeEdit(hwnd_, std::to_wstring(m.horizontal), 282, 110, 60, 26, kIdEdit2, ES_NUMBER | ES_CENTER);
        lblPx_ = MakeLabel(hwnd_, L"px", 348, 114, 28, 20);

        const wchar_t* label = current_ == ViewMode::MediumIcons ? L"보통 아이콘" : current_ == ViewMode::LargeIcons ? L"큰 아이콘" : L"큰 아이콘 (현재: 자세히)";
        hint_ = MakeLabel(hwnd_, (std::wstring(L"* 현재 보기 모드(") + label + L")의 상하 x 좌우 여백입니다.").c_str(), 154, 146, 320, 36);

        // --- 단축키 카테고리 ---
        titleKb_ = MakeLabel(hwnd_, L"키보드", 150, 16, 52, 22);
        SendMessageW(titleKb_, WM_SETFONT, reinterpret_cast<WPARAM>(ui::fontBold), TRUE);
        MakeShortcutRows();
        int resetY = 66 + static_cast<int>(workingShortcuts_.size()) * 28 + 6;
        btnResetSc_ = MakeControl(hwnd_, L"BUTTON", L"기본값으로 복원", WS_TABSTOP | BS_OWNERDRAW, 150, resetY, 130, 26, kIdBtnResetShortcut);
        MakeControl(hwnd_, L"BUTTON", L"확인", WS_TABSTOP | BS_OWNERDRAW, 448, 382, 75, 26, IDOK);
        MakeControl(hwnd_, L"BUTTON", L"취소", WS_TABSTOP | BS_OWNERDRAW, 531, 382, 75, 26, IDCANCEL);

        SwitchCategory(Category::Visual);
    }

    void OnShown() override {
        SetFocus(vert_);
        SendMessageW(vert_, EM_SETSEL, 0, -1);
    }

    void SwitchCategory(Category cat) {
        activeCategory_ = cat;
        bool isVis = (cat == Category::Visual);
        for (HWND h : {titleTheme_, chkDarkMode_, titleIcon_, lblMargin_, vert_, lblX_, horiz_, lblPx_, hint_})
            if (h) ShowWindow(h, isVis ? SW_SHOW : SW_HIDE);
        for (HWND h : shortcutControls_) ShowWindow(h, isVis ? SW_HIDE : SW_SHOW);
        for (HWND h : {titleKb_, btnResetSc_}) ShowWindow(h, isVis ? SW_HIDE : SW_SHOW);
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }

    void MakeShortcutRows() {
        shortcutControls_.push_back(MakeLabel(hwnd_, L"항목", 150, 42, 180, 20));
        shortcutControls_.push_back(MakeLabel(hwnd_, L"단축키 1", 340, 42, 120, 20));
        shortcutControls_.push_back(MakeLabel(hwnd_, L"단축키 2", 474, 42, 120, 20));
        for (size_t i = 0; i < workingShortcuts_.size(); ++i) {
            int y = 66 + static_cast<int>(i) * 28;
            shortcutControls_.push_back(MakeLabel(hwnd_, workingShortcuts_[i].name.c_str(), 150, y + 3, 185, 24));
            for (int slot = 0; slot < 2; ++slot) {
                HWND e = MakeEdit(hwnd_, slot == 0 ? workingShortcuts_[i].shortcut : workingShortcuts_[i].shortcut2,
                                  340 + slot * 134, y, 124, 24, 2000 + static_cast<int>(i) * 2 + slot, ES_READONLY);
                shortcutEdits_.push_back(e);
                shortcutControls_.push_back(e);
            }
        }
    }

    bool FilterMessage(const MSG& msg) override {
        auto it = std::find(shortcutEdits_.begin(), shortcutEdits_.end(), msg.hwnd);
        if (it == shortcutEdits_.end()) return false;
        if (msg.message == WM_CHAR || msg.message == WM_SYSCHAR) return true;
        if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) return false;
        unsigned key = static_cast<unsigned>(msg.wParam);
        if (key == VK_CONTROL || key == VK_SHIFT || key == VK_MENU || key == VK_LWIN || key == VK_RWIN) return true;
        bool ctrl = GetKeyState(VK_CONTROL) < 0, alt = GetKeyState(VK_MENU) < 0, shift = GetKeyState(VK_SHIFT) < 0;
        if (IsFixedShortcutKey(key, ctrl, alt, shift)) {
            Warn(L"이 키는 고정 단축키로 사용 중입니다.", L"단축키 설정");
            return true;
        }
        std::wstring value;
        if (ctrl) value += L"Ctrl+";
        if (alt) value += L"Alt+";
        if (shift) value += L"Shift+";
        if (key == VK_ESCAPE && !ctrl && !alt && !shift) value.clear(); // Esc: 단축키 해제
        else if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) value += static_cast<wchar_t>(key);
        else if (key >= VK_F1 && key <= VK_F12) value += L"F" + std::to_wstring(key - VK_F1 + 1);
        else {
            switch (key) {
                case VK_BACK: value += L"Backspace"; break;
                case VK_DELETE: value += L"Del"; break;
                case VK_RETURN: value += L"Enter"; break;
                case VK_LEFT: value += L"Left"; break;
                case VK_RIGHT: value += L"Right"; break;
                case VK_UP: value += L"Up"; break;
                case VK_DOWN: value += L"Down"; break;
                case VK_TAB: value += L"Tab"; break;
                case VK_SPACE: value += L"Space"; break;
                case VK_ESCAPE: value += L"Esc"; break;
                default: value += L"VK" + std::to_wstring(key); break;
            }
        }
        if (ctrl && !alt && key == VK_TAB) {
            Warn(L"Ctrl+Tab 및 Ctrl+Shift+Tab은 탭 전환에 예약되어 있습니다.", L"단축키 설정");
            return true;
        }
        size_t index = static_cast<size_t>(it - shortcutEdits_.begin());
        auto& item = workingShortcuts_[index / 2];
        (index % 2 ? item.shortcut2 : item.shortcut) = value;
        SetWindowTextW(msg.hwnd, value.c_str());
        return true;
    }

    void OnCommand(int id) override {
        if (id == kIdCategoryVisual) SwitchCategory(Category::Visual);
        else if (id == kIdCategoryShortcuts) SwitchCategory(Category::Shortcuts);
        else if (id == kIdChkDarkMode) {
            darkChecked_ = !darkChecked_;
            InvalidateRect(chkDarkMode_, nullptr, TRUE);
        } else if (id == kIdBtnResetShortcut) {
            workingShortcuts_ = DefaultShortcuts();
            for (size_t i = 0; i < shortcutEdits_.size(); ++i)
                SetWindowTextW(shortcutEdits_[i], (i % 2 ? workingShortcuts_[i / 2].shortcut2 : workingShortcuts_[i / 2].shortcut).c_str());
        }
    }

    LRESULT OnMessage(UINT msg, WPARAM wParam, LPARAM lParam, bool& handled) override {
        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC dc = BeginPaint(hwnd_, &ps);
                RECT cr;
                GetClientRect(hwnd_, &cr);
                const int leftW = ui::S(130);
                const int botY = cr.bottom - ui::S(48);

                // 왼쪽 카테고리 배경
                RECT leftRc{0, 0, leftW, botY};
                ui::FillRectColor(dc, leftRc, ui::kStripBg);

                // 오른쪽 내용 배경
                RECT rightRc{leftW, 0, cr.right, botY};
                ui::FillRectColor(dc, rightRc, ui::kPanelBg);

                // 하단 버튼바 배경
                RECT botRc{0, botY, cr.right, cr.bottom};
                ui::FillRectColor(dc, botRc, ui::kStripBg);

                // 구분선: 왼쪽-오른쪽
                RECT vDiv{leftW - ui::S(1), 0, leftW, botY};
                ui::FillRectColor(dc, vDiv, ui::kBorder);

                // 구분선: 하단 버튼바
                RECT hDiv{0, botY, cr.right, botY + ui::S(1)};
                ui::FillRectColor(dc, hDiv, ui::kBorder);

                const int lineRight = cr.right - ui::S(20);

                if (activeCategory_ == Category::Visual) {
                    // 제목 "테마" 오른쪽 여백 가로줄
                    const int themeLineLeft = ui::S(150 + 42 + 8);
                    const int themeLineY = ui::S(27);
                    if (lineRight > themeLineLeft) {
                        RECT themeLine{themeLineLeft, themeLineY, lineRight, themeLineY + ui::S(1)};
                        ui::FillRectColor(dc, themeLine, ui::kBorder);
                    }

                    // 제목 "아이콘" 오른쪽 여백 가로줄
                    const int iconLineLeft = ui::S(150 + 52 + 8);
                    const int iconLineY = ui::S(91);
                    if (lineRight > iconLineLeft) {
                        RECT iconLine{iconLineLeft, iconLineY, lineRight, iconLineY + ui::S(1)};
                        ui::FillRectColor(dc, iconLine, ui::kBorder);
                    }
                } else if (activeCategory_ == Category::Shortcuts) {
                    // 제목 "키보드" 오른쪽 여백 가로줄
                    const int kbLineLeft = ui::S(150 + 52 + 8);
                    const int kbLineY = ui::S(27);
                    if (lineRight > kbLineLeft) {
                        RECT kbLine{kbLineLeft, kbLineY, lineRight, kbLineY + ui::S(1)};
                        ui::FillRectColor(dc, kbLine, ui::kBorder);
                    }
                }

                EndPaint(hwnd_, &ps);
                handled = true;
                return 0;
            }
            case WM_ERASEBKGND:
                handled = true;
                return 1;
            case WM_DRAWITEM: {
                auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                if (!dis) break;
                HDC dc = dis->hDC;
                RECT rc = dis->rcItem;

                if (dis->CtlID == kIdCategoryVisual || dis->CtlID == kIdCategoryShortcuts) {
                    bool isSelected = (dis->CtlID == kIdCategoryVisual && activeCategory_ == Category::Visual) ||
                                      (dis->CtlID == kIdCategoryShortcuts && activeCategory_ == Category::Shortcuts);
                    if (isSelected) {
                        ui::FillRectColor(dc, rc, ui::kPanelBg);
                        ui::FrameRectColor(dc, rc, ui::kBorder);
                        RECT bar{rc.left, rc.top, rc.left + ui::S(3), rc.bottom};
                        ui::FillRectColor(dc, bar, ui::kAccent);
                        RECT tr = rc;
                        tr.left += ui::S(12);
                        ui::DrawTextIn(dc, dis->CtlID == kIdCategoryVisual ? L"비주얼" : L"단축키", tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE, ui::kText, ui::fontBold);
                    } else {
                        ui::FillRectColor(dc, rc, ui::kStripBg);
                        RECT tr = rc;
                        tr.left += ui::S(12);
                        ui::DrawTextIn(dc, dis->CtlID == kIdCategoryVisual ? L"비주얼" : L"단축키", tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE, ui::kTextDim, ui::font);
                    }
                    handled = true;
                    return TRUE;
                }

                if (dis->CtlID == kIdChkDarkMode) {
                    ui::FillRectColor(dc, rc, ui::kPanelBg);
                    // 체크박스 상자
                    RECT box{rc.left + ui::S(2), rc.top + ui::S(3), rc.left + ui::S(18), rc.top + ui::S(19)};
                    ui::FillRectColor(dc, box, darkChecked_ ? ui::kAccent : ui::kFieldBg);
                    ui::FrameRectColor(dc, box, darkChecked_ ? ui::kAccent : ui::kBorderIdle);
                    if (darkChecked_) {
                        HPEN pen = CreatePen(PS_SOLID, ui::S(2), RGB(255, 255, 255));
                        HGDIOBJ oldPen = SelectObject(dc, pen);
                        MoveToEx(dc, box.left + ui::S(3), box.top + ui::S(8), nullptr);
                        LineTo(dc, box.left + ui::S(6), box.top + ui::S(12));
                        LineTo(dc, box.left + ui::S(13), box.top + ui::S(4));
                        SelectObject(dc, oldPen);
                        DeleteObject(pen);
                    }
                    RECT tr{rc.left + ui::S(24), rc.top, rc.right, rc.bottom};
                    ui::DrawTextIn(dc, L"다크 모드", tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE, ui::kText, ui::font);
                    handled = true;
                    return TRUE;
                }

                if (dis->CtlID == IDOK || dis->CtlID == IDCANCEL || dis->CtlID == kIdBtnChangeShortcut || dis->CtlID == kIdBtnResetShortcut) {
                    bool isDefault = (dis->CtlID == IDOK);
                    bool isPressed = (dis->itemState & ODS_SELECTED) != 0;
                    COLORREF bg = isPressed ? ui::kPressed : isDefault ? ui::kAccent : ui::kButtonBg;
                    COLORREF border = isDefault ? ui::kAccent : ui::kButtonBorder;
                    COLORREF textCol = isDefault ? RGB(255, 255, 255) : ui::kText;
                    ui::FillRectColor(dc, rc, bg);
                    ui::FrameRectColor(dc, rc, border);
                    wchar_t btnText[64]{};
                    GetWindowTextW(dis->hwndItem, btnText, 64);
                    ui::DrawTextIn(dc, btnText, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE, textCol, ui::font);
                    handled = true;
                    return TRUE;
                }
                break;
            }
            case WM_CTLCOLORSTATIC: {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                HWND ctl = reinterpret_cast<HWND>(lParam);
                SetBkMode(hdc, TRANSPARENT);
                if (ctl == hint_) {
                    SetTextColor(hdc, ui::kTextDim);
                } else {
                    SetTextColor(hdc, ui::kText);
                }
                handled = true;
                return reinterpret_cast<LRESULT>(panelBrush_);
            }
            case WM_CTLCOLOREDIT: {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                SetBkColor(hdc, ui::kPanelBg);
                SetTextColor(hdc, ui::kText);
                handled = true;
                return reinterpret_cast<LRESULT>(panelBrush_);
            }
            case WM_CTLCOLORLISTBOX: {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                SetBkColor(hdc, ui::kPanelBg);
                SetTextColor(hdc, ui::kText);
                handled = true;
                return reinterpret_cast<LRESULT>(panelBrush_);
            }
            default: break;
        }
        return 0;
    }

    bool OnOk() override {
        int v = 0, h = 0;
        if (!ParseMargin(vert_, v)) {
            Warn(L"상하 여백 값은 0에서 300 사이의 숫자를 입력해 주세요.", L"설정 오류");
            SwitchCategory(Category::Visual);
            SetFocus(vert_);
            return false;
        }
        if (!ParseMargin(horiz_, h)) {
            Warn(L"좌우 여백 값은 0에서 300 사이의 숫자를 입력해 주세요.", L"설정 오류");
            SwitchCategory(Category::Visual);
            SetFocus(horiz_);
            return false;
        }

        // 같은 키를 서로 다른 기능에 중복 지정하지 않는다.
        for (size_t i = 0; i < workingShortcuts_.size(); ++i) {
            for (size_t j = i + 1; j < workingShortcuts_.size(); ++j) {
                for (const auto* a : {&workingShortcuts_[i].shortcut, &workingShortcuts_[i].shortcut2})
                    for (const auto* b : {&workingShortcuts_[j].shortcut, &workingShortcuts_[j].shortcut2})
                        if (!a->empty() && EqualsNoCase(*a, *b)) {
                            Warn(L"서로 다른 기능에 동일한 단축키가 지정되어 있습니다.", L"단축키 설정");
                            SwitchCategory(Category::Shortcuts);
                            return false;
                        }
            }
        }
        // 확인 시점에만 변경된 다크 모드 및 설정 적용
        if (darkChecked_ != origDarkMode_) {
            ui::SetDarkMode(darkChecked_);
        }
        auto& settings = SettingsService::Get();
        settings.state.darkMode = darkChecked_;
        settings.Margins(target_) = {v, h};
        if (current_ == ViewMode::Details) settings.state.mediumIconMargins = {v, h};
        settings.state.shortcuts = workingShortcuts_;

        std::wstring error;
        if (!settings.Save(&error)) Warn((L"설정을 저장하지 못했습니다: " + error).c_str(), L"설정");

        return true;
    }

private:
    static bool ParseMargin(HWND edit, int& out) {
        std::wstring s = Trim(ui::GetText(edit));
        if (s.empty() || s.size() > 4) return false;
        int v = 0;
        for (wchar_t c : s) {
            if (c < L'0' || c > L'9') return false;
            v = v * 10 + (c - L'0');
        }
        if (v < 0 || v > 300) return false;
        out = v;
        return true;
    }

    ViewMode current_, target_;
    Category activeCategory_ = Category::Visual;
    bool origDarkMode_ = false;
    bool darkChecked_ = false;
    std::vector<ShortcutItem> workingShortcuts_;

    HWND catVisual_ = nullptr;
    HWND catShortcuts_ = nullptr;

    HWND titleTheme_ = nullptr;
    HWND chkDarkMode_ = nullptr;
    HWND titleIcon_ = nullptr;
    HWND lblMargin_ = nullptr;
    HWND vert_ = nullptr;
    HWND lblX_ = nullptr;
    HWND horiz_ = nullptr;
    HWND lblPx_ = nullptr;
    HWND hint_ = nullptr;

    HWND titleKb_ = nullptr;
    std::vector<HWND> shortcutControls_;
    std::vector<HWND> shortcutEdits_;
    HWND btnResetSc_ = nullptr;

    HBRUSH panelBrush_ = nullptr;
};

// ---------- 폴더 그룹 수정 ----------
class GroupEditDlg final : public Dialog {
public:
    explicit GroupEditDlg(FolderGroup& g) : g_(g), paths_(g.paths) {}

protected:
    void OnCreate() override {
        MakeLabel(hwnd_, L"그룹 이름", 14, 12, 300, 18);
        name_ = MakeEdit(hwnd_, g_.name, 14, 32, 462, 26, kIdEdit1);
        MakeLabel(hwnd_, L"포함된 폴더 목록", 14, 68, 300, 18);
        list_ = MakeControl(hwnd_, L"LISTBOX", L"", WS_BORDER | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 14, 88, 462, 150, kIdList);
        MakeButton(hwnd_, L"폴더 추가", 14, 246, 78, 26, kIdAdd);
        MakeButton(hwnd_, L"폴더 수정", 98, 246, 78, 26, kIdEdit);
        MakeButton(hwnd_, L"폴더 삭제", 182, 246, 78, 26, kIdRemove);
        MakeButton(hwnd_, L"▲", 270, 246, 30, 26, kIdUp);
        MakeButton(hwnd_, L"▼", 304, 246, 30, 26, kIdDown);
        MakeButton(hwnd_, L"저장", 316, 288, 75, 28, IDOK, true);
        MakeButton(hwnd_, L"취소", 400, 288, 75, 28, IDCANCEL);
        Fill(-1);
    }
    void OnShown() override {
        SetFocus(name_);
        SendMessageW(name_, EM_SETSEL, 0, -1);
    }
    void OnCommand(int id) override {
        int idx = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
        switch (id) {
            case kIdAdd: {
                std::wstring picked;
                if (PickFolder(hwnd_, L"그룹에 추가할 폴더 선택", L"", picked)) {
                    paths_.push_back(picked);
                    Fill(static_cast<int>(paths_.size()) - 1);
                }
                break;
            }
            case kIdEdit: {
                if (idx < 0 || idx >= static_cast<int>(paths_.size())) {
                    MessageBoxW(hwnd_, L"수정할 폴더를 선택하세요.", L"폴더 수정", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                std::wstring picked;
                const std::wstring& current = paths_[static_cast<size_t>(idx)];
                if (PickFolder(hwnd_, L"변경할 폴더 선택", IsDirectoryPath(current) ? current : L"", picked)) {
                    paths_[static_cast<size_t>(idx)] = picked;
                    Fill(idx);
                }
                break;
            }
            case kIdRemove:
                if (idx < 0 || idx >= static_cast<int>(paths_.size())) {
                    MessageBoxW(hwnd_, L"삭제할 폴더를 선택하세요.", L"폴더 삭제", MB_OK | MB_ICONINFORMATION);
                } else {
                    paths_.erase(paths_.begin() + idx);
                    Fill(paths_.empty() ? -1 : std::min(idx, static_cast<int>(paths_.size()) - 1));
                }
                break;
            case kIdUp:
                if (idx > 0) {
                    std::swap(paths_[static_cast<size_t>(idx)], paths_[static_cast<size_t>(idx - 1)]);
                    Fill(idx - 1);
                }
                break;
            case kIdDown:
                if (idx >= 0 && idx < static_cast<int>(paths_.size()) - 1) {
                    std::swap(paths_[static_cast<size_t>(idx)], paths_[static_cast<size_t>(idx + 1)]);
                    Fill(idx + 1);
                }
                break;
            default: break;
        }
    }
    bool OnOk() override {
        std::wstring name = Trim(ui::GetText(name_));
        if (name.empty()) {
            Warn(L"그룹 이름을 입력하세요.", L"폴더 그룹 수정");
            SetFocus(name_);
            return false;
        }
        // 폴더 순서가 바뀌거나 삭제되면 탭별 보기 모드 및 색상 목록도 같은 순서로 맞춘다.
        std::vector<ViewMode> newModes;
        std::vector<uint32_t> newColors;
        for (const auto& p : paths_) {
            ViewMode m = g_.mode;
            uint32_t c = CLR_INVALID;
            for (size_t i = 0; i < g_.paths.size(); ++i) {
                if (EqualsNoCase(g_.paths[i], p)) {
                    if (i < g_.modes.size()) m = g_.modes[i];
                    if (i < g_.colors.size()) c = g_.colors[i];
                }
            }
            newModes.push_back(m);
            newColors.push_back(c);
        }
        g_.name = name;
        g_.paths = paths_;
        g_.modes = newModes;
        g_.colors = newColors;
        return true;
    }

private:
    void Fill(int select) {
        SendMessageW(list_, LB_RESETCONTENT, 0, 0);
        for (const auto& p : paths_) SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p.c_str()));
        if (select >= 0) SendMessageW(list_, LB_SETCURSEL, static_cast<WPARAM>(select), 0);
    }
    FolderGroup& g_;
    std::vector<std::wstring> paths_;
    HWND name_ = nullptr, list_ = nullptr;
};

// ---------- 폴더 그룹 관리 ----------
class GroupManagerDlg final : public Dialog {
protected:
    void OnCreate() override {
        MakeLabel(hwnd_, L"저장된 폴더 그룹 목록", 14, 12, 300, 18);
        list_ = MakeControl(hwnd_, L"LISTBOX", L"", WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 14, 34, 330, 170, kIdList);
        MakeButton(hwnd_, L"수정", 14, 214, 72, 26, kIdEdit);
        MakeButton(hwnd_, L"삭제", 92, 214, 72, 26, kIdRemove);
        MakeButton(hwnd_, L"닫기", 272, 214, 72, 26, IDOK, true);
        Fill(-1);
    }
    bool OnOk() override { return true; }
    void OnCommand(int id) override {
        auto& settings = SettingsService::Get();
        int idx = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
        bool valid = idx >= 0 && idx < static_cast<int>(settings.state.folderGroups.size());
        if (id == kIdEdit) {
            if (!valid) {
                MessageBoxW(hwnd_, L"수정할 그룹을 선택하세요.", L"폴더 그룹 관리", MB_OK | MB_ICONINFORMATION);
                return;
            }
            FolderGroup copy = settings.state.folderGroups[static_cast<size_t>(idx)];
            if (FolderGroupEditDialog(hwnd_, copy)) {
                settings.state.folderGroups[static_cast<size_t>(idx)] = copy;
                Save();
                Fill(idx);
            }
        } else if (id == kIdRemove) {
            if (!valid) {
                MessageBoxW(hwnd_, L"삭제할 그룹을 선택하세요.", L"폴더 그룹 관리", MB_OK | MB_ICONINFORMATION);
                return;
            }
            const std::wstring& name = settings.state.folderGroups[static_cast<size_t>(idx)].name;
            if (MessageBoxW(hwnd_, (L"폴더 그룹 '" + name + L"'을(를) 삭제하시겠습니까?").c_str(), L"폴더 그룹 삭제", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                settings.state.folderGroups.erase(settings.state.folderGroups.begin() + idx);
                Save();
                Fill(-1);
            }
        }
    }

private:
    void Save() {
        std::wstring error;
        if (!SettingsService::Get().Save(&error)) Warn((L"설정을 저장하지 못했습니다: " + error).c_str(), L"폴더 그룹 관리");
    }
    void Fill(int select) {
        SendMessageW(list_, LB_RESETCONTENT, 0, 0);
        for (const auto& g : SettingsService::Get().state.folderGroups) {
            std::wstring line = g.name + L"  (폴더 " + std::to_wstring(g.paths.size()) + L"개)";
            SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
        }
        if (select >= 0) SendMessageW(list_, LB_SETCURSEL, static_cast<WPARAM>(select), 0);
    }
    HWND list_ = nullptr;
};

}  // namespace

bool InputDialog(HWND owner, const wchar_t* title, const wchar_t* prompt, std::wstring& text, int selStart, int selEnd,
                 const wchar_t* okLabel, const NameValidator& validate) {
    InputDlg dlg(prompt, text, selStart, selEnd, okLabel, validate);
    return dlg.Run(owner, title, 420, 146);
}

bool BookmarkDialog(HWND owner, Bookmark& bookmark) {
    BookmarkDlg dlg(bookmark);
    return dlg.Run(owner, L"북마크 수정", 440, 176);
}

bool SettingsDialog(HWND owner, ViewMode currentMode) {
    SettingsDlg dlg(currentMode);
    return dlg.Run(owner, L"설정", 620, 420);
}

bool FolderGroupEditDialog(HWND owner, FolderGroup& group) {
    GroupEditDlg dlg(group);
    return dlg.Run(owner, L"폴더 그룹 수정", 490, 334);
}

void FolderGroupManagerDialog(HWND owner) {
    GroupManagerDlg dlg;
    dlg.Run(owner, L"폴더 그룹 관리", 358, 258);
}

bool PickFolder(HWND owner, const wchar_t* title, const std::wstring& initial, std::wstring& out) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))) || !dlg) return false;
    DWORD options = 0;
    dlg->GetOptions(&options);
    dlg->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(title);
    if (!initial.empty()) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&item))) && item) {
            dlg->SetFolder(item);
            item->Release();
        }
    }
    bool ok = false;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* result = nullptr;
        if (SUCCEEDED(dlg->GetResult(&result)) && result) {
            PWSTR p = nullptr;
            if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
                out = p;
                ok = !out.empty();
                CoTaskMemFree(p);
            }
            result->Release();
        }
    }
    dlg->Release();
    return ok;
}

}  // namespace ft
