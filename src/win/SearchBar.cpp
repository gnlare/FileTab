#include "SearchBar.h"
#include "SearchIcons.h"
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>

namespace ft {

namespace {
const wchar_t* kSearchBarClass = L"FileTabSearchBar";
const wchar_t* kFilterPopupClass = L"FileTabFilterPopup";
constexpr UINT_PTR kEditSubclassId = 101;

enum FilterId : int {
    kIdTypeCombo = 301,
    kIdSubfoldersChk = 302,
    kIdContentEdit = 303,
    kIdSearchBtn = 304
};
}  // namespace

// ======================= Filter Popup =======================

class FilterPopup {
public:
    static HWND Create(HWND parent, SearchBar* bar, const TabSearchState& state) {
        auto* fp = new FilterPopup(bar, state);
        HINSTANCE inst = GetModuleHandleW(nullptr);
        HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kFilterPopupClass, L"",
                                    WS_POPUP | WS_BORDER, 0, 0, 10, 10, parent, nullptr, inst, fp);
        if (!hwnd) {
            delete fp;
            return nullptr;
        }
        return hwnd;
    }

    ~FilterPopup() {
        if (bgBrush_) DeleteObject(bgBrush_);
    }

private:
    explicit FilterPopup(SearchBar* bar, const TabSearchState& state) : bar_(bar), state_(state) {}

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        FilterPopup* self;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<FilterPopup*>(cs->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<FilterPopup*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
        return self->HandleMessage(msg, wParam, lParam);
    }

    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
            case WM_CREATE: CreateControls(); return 0;
            case WM_ACTIVATE:
                if (LOWORD(wParam) == WA_INACTIVE) {
                    SaveAndClose(false);
                }
                return 0;
            case WM_COMMAND:
                if (LOWORD(wParam) == kIdSearchBtn) {
                    SaveAndClose(true);
                    return 0;
                }
                break;
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLORBTN: {
                HDC dc = reinterpret_cast<HDC>(wParam);
                SetTextColor(dc, ui::kText);
                SetBkMode(dc, TRANSPARENT);
                if (!bgBrush_) bgBrush_ = CreateSolidBrush(ui::IsDarkMode() ? RGB(37, 37, 38) : RGB(245, 245, 245));
                return reinterpret_cast<LRESULT>(bgBrush_);
            }
            case WM_CTLCOLOREDIT: {
                HDC dc = reinterpret_cast<HDC>(wParam);
                SetTextColor(dc, ui::kText);
                SetBkColor(dc, ui::IsDarkMode() ? RGB(45, 45, 48) : RGB(255, 255, 255));
                if (!editBrush_) editBrush_ = CreateSolidBrush(ui::IsDarkMode() ? RGB(45, 45, 48) : RGB(255, 255, 255));
                return reinterpret_cast<LRESULT>(editBrush_);
            }
            case WM_DESTROY:
                if (tip_) { DestroyWindow(tip_); tip_ = nullptr; }
                if (bgBrush_) { DeleteObject(bgBrush_); bgBrush_ = nullptr; }
                if (editBrush_) { DeleteObject(editBrush_); editBrush_ = nullptr; }
                if (bar_) bar_->OnPopupClosed();
                delete this;
                return 0;
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }

    void CreateControls() {
        HINSTANCE inst = GetModuleHandleW(nullptr);
        auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
            HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, ui::S(x), ui::S(y), ui::S(w), ui::S(h),
                                     hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(ui::font), TRUE);
            return c;
        };

        make(L"STATIC", L"유형", SS_LEFT, 14, 14, 40, 20, -1);
        comboType_ = make(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 60, 11, 184, 150, kIdTypeCombo);
        SendMessageW(comboType_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"모든 폴더와 파일"));
        SendMessageW(comboType_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"모든 파일"));
        SendMessageW(comboType_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"이미지"));
        SendMessageW(comboType_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"영상"));
        SendMessageW(comboType_, CB_SETCURSEL, static_cast<WPARAM>(state_.type), 0);

        chkSub_ = make(L"BUTTON", L"하위 폴더 검색", WS_TABSTOP | BS_AUTOCHECKBOX, 14, 46, 230, 24, kIdSubfoldersChk);
        SendMessageW(chkSub_, BM_SETCHECK, state_.searchSubfolders ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND lblContent = make(L"STATIC", L"포함 문자", SS_LEFT | SS_NOTIFY, 14, 78, 62, 20, -1);
        editContent_ = make(L"EDIT", state_.contentQuery.c_str(), WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 80, 76, 164, 24, kIdContentEdit);

        tip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd_, nullptr, inst, nullptr);
        if (tip_) {
            TOOLINFOW ti{};
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd = hwnd_;
            ti.lpszText = const_cast<LPWSTR>(L"텍스트 파일 내 포함 문자 검색");

            ti.uId = reinterpret_cast<UINT_PTR>(lblContent);
            SendMessageW(tip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));

            ti.uId = reinterpret_cast<UINT_PTR>(editContent_);
            SendMessageW(tip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
        }

        make(L"BUTTON", L"검색", WS_TABSTOP | BS_PUSHBUTTON, 174, 108, 70, 26, kIdSearchBtn);
    }

    void SaveAndClose(bool triggerSearch) {
        if (!hwnd_) return;
        int typeIdx = static_cast<int>(SendMessageW(comboType_, CB_GETCURSEL, 0, 0));
        if (typeIdx >= 0 && typeIdx <= 3) state_.type = static_cast<SearchType>(typeIdx);

        state_.searchSubfolders = (SendMessageW(chkSub_, BM_GETCHECK, 0, 0) == BST_CHECKED);

        int len = GetWindowTextLengthW(editContent_);
        std::wstring content(len + 1, L'\0');
        GetWindowTextW(editContent_, content.data(), len + 1);
        content.resize(len);
        state_.contentQuery = content;

        auto& g = GlobalSearchOptions();
        g.type = state_.type;
        g.searchSubfolders = state_.searchSubfolders;
        g.contentQuery = state_.contentQuery;

        HWND w = hwnd_;
        hwnd_ = nullptr;
        DestroyWindow(w);

        if (bar_) {
            bar_->OnPopupClosed();
            bar_->SetState(state_);
            if (triggerSearch) bar_->CommitSearch();
        }
    }

    HWND hwnd_ = nullptr;
    SearchBar* bar_ = nullptr;
    TabSearchState state_;
    HWND comboType_ = nullptr;
    HWND chkSub_ = nullptr;
    HWND editContent_ = nullptr;
    HWND tip_ = nullptr;
    HBRUSH bgBrush_ = nullptr;
    HBRUSH editBrush_ = nullptr;

    friend class SearchBar;
};

// ======================= SearchBar =======================

bool SearchBar::RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &SearchBar::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kSearchBarClass;
    if (!RegisterClassExW(&wc)) return false;

    WNDCLASSEXW pc{};
    pc.cbSize = sizeof pc;
    pc.lpfnWndProc = &FilterPopup::WndProc;
    pc.hInstance = instance;
    pc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    pc.hbrBackground = nullptr;
    pc.lpszClassName = kFilterPopupClass;
    return RegisterClassExW(&pc) != 0;
}

SearchBar* SearchBar::Create(HWND parent, Callbacks cb) {
    auto* bar = new SearchBar();
    bar->cb_ = std::move(cb);
    HINSTANCE inst = GetModuleHandleW(nullptr);
    bar->hwnd_ = CreateWindowExW(0, kSearchBarClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_TABSTOP,
                                 0, 0, 10, 10, parent, nullptr, inst, bar);
    if (!bar->hwnd_) {
        delete bar;
        return nullptr;
    }

    bar->edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 0, 0, 10, 10, bar->hwnd_, reinterpret_cast<HMENU>(101), inst, nullptr);
    if (bar->edit_) {
        SendMessageW(bar->edit_, WM_SETFONT, reinterpret_cast<WPARAM>(ui::font), TRUE);
        SetWindowSubclass(bar->edit_, &SearchBar::EditProc, kEditSubclassId, reinterpret_cast<DWORD_PTR>(bar));
    }
    bar->LayoutChildren();
    return bar;
}

SearchBar::~SearchBar() {
    if (popup_) DestroyWindow(popup_);
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
}

LRESULT CALLBACK SearchBar::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SearchBar* self;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SearchBar*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<SearchBar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->HandleMessage(msg, wParam, lParam);
}

LRESULT CALLBACK SearchBar::EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    auto* self = reinterpret_cast<SearchBar*>(dwRefData);
    if (self) {
        switch (msg) {
            case WM_KEYDOWN:
                if (wParam == VK_RETURN) {
                    self->CommitSearch();
                    return 0;
                }
                if (wParam == VK_ESCAPE) {
                    self->CancelSearch();
                    return 0;
                }
                break;
            case WM_CHAR:
                if (wParam == VK_RETURN || wParam == VK_ESCAPE) return 0;
                break;
            case WM_SETFOCUS:
            case WM_KILLFOCUS:
                InvalidateRect(self->hwnd_, nullptr, FALSE);
                break;
            case WM_NCDESTROY:
                RemoveWindowSubclass(hwnd, &SearchBar::EditProc, uIdSubclass);
                break;
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT SearchBar::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: Paint(); return 0;
        case WM_SIZE: LayoutChildren(); return 0;
        case WM_SETFOCUS:
            if (edit_) SetFocus(edit_);
            return 0;
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, ui::kText);
            SetBkColor(dc, ui::IsDarkMode() ? RGB(45, 45, 48) : RGB(255, 255, 255));
            static HBRUSH s_editBrushDark = CreateSolidBrush(RGB(45, 45, 48));
            static HBRUSH s_editBrushLight = CreateSolidBrush(RGB(255, 255, 255));
            return reinterpret_cast<LRESULT>(ui::IsDarkMode() ? s_editBrushDark : s_editBrushLight);
        }
        case WM_MOUSEMOVE: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            bool hotA = PtInRect(&rcAction_, pt) != FALSE;
            bool hotF = PtInRect(&rcFilter_, pt) != FALSE;
            if (hotA != hotAction_ || hotF != hotFilter_) {
                hotAction_ = hotA;
                hotFilter_ = hotF;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            if (!trackLeave_) {
                TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                trackLeave_ = true;
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            trackLeave_ = false;
            if (hotAction_ || hotFilter_) {
                hotAction_ = hotFilter_ = false;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case WM_SETCURSOR: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd_, &pt);
            if (PtInRect(&rcAction_, pt) || PtInRect(&rcFilter_, pt)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (PtInRect(&rcAction_, pt)) {
                if (state_.active) CancelSearch();
                else CommitSearch();
                return 0;
            }
            if (PtInRect(&rcFilter_, pt)) {
                ToggleFilterPopup();
                return 0;
            }
            if (edit_) SetFocus(edit_);
            return 0;
        }
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

void SearchBar::LayoutChildren() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    int h = rc.bottom;
    int btnW = ui::S(22);

    rcFilter_ = {rc.right - btnW - ui::S(3), (h - btnW) / 2, rc.right - ui::S(3), (h + btnW) / 2};
    rcAction_ = {rcFilter_.left - btnW - ui::S(2), (h - btnW) / 2, rcFilter_.left - ui::S(2), (h + btnW) / 2};

    int editX = ui::S(8);
    int editW = std::max(10, static_cast<int>(rcAction_.left - editX - ui::S(4)));
    int editH = ui::S(18);
    if (edit_) MoveWindow(edit_, editX, (h - editH) / 2, editW, editH, TRUE);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SearchBar::Paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd_, &ps);
    RECT rc{};
    GetClientRect(hwnd_, &rc);

    bool dark = ui::IsDarkMode();
    bool focused = (GetFocus() == edit_ || GetFocus() == hwnd_);

    // 배경 및 테두리
    COLORREF bgCol = dark ? RGB(45, 45, 48) : RGB(255, 255, 255);
    COLORREF borderCol = focused ? RGB(0, 120, 215) : (dark ? RGB(70, 70, 72) : RGB(200, 200, 200));

    HBRUSH bgBrush = CreateSolidBrush(bgCol);
    HPEN borderPen = CreatePen(PS_SOLID, 1, borderCol);
    HGDIOBJ oldBrush = SelectObject(dc, bgBrush);
    HGDIOBJ oldPen = SelectObject(dc, borderPen);

    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, ui::S(4), ui::S(4));

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(borderPen);
    DeleteObject(bgBrush);

    // Placeholder 표시
    if (edit_ && GetWindowTextLengthW(edit_) == 0 && !focused) {
        RECT phRc = rc;
        phRc.left += ui::S(8);
        phRc.right = rcAction_.left - ui::S(4);
        ui::DrawTextIn(dc, L"찾기 (Ctrl+F)", phRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE, ui::kTextDim, ui::font);
    }

    // 버튼 호버 배경
    auto drawBtnBg = [&](const RECT& r, bool hot) {
        if (!hot) return;
        COLORREF hCol = dark ? RGB(60, 60, 65) : RGB(230, 230, 235);
        HBRUSH hb = CreateSolidBrush(hCol);
        HGDIOBJ ob = SelectObject(dc, hb);
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, r.left, r.top, r.right, r.bottom, ui::S(4), ui::S(4));
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(hb);
    };

    drawBtnBg(rcAction_, hotAction_);
    drawBtnBg(rcFilter_, hotFilter_);

    HDC scratchDc = CreateCompatibleDC(dc);

    // 1. 동작 아이콘 (돋보기 또는 X)
    int cxA = (rcAction_.left + rcAction_.right) / 2;
    int cyA = (rcAction_.top + rcAction_.bottom) / 2;

    if (state_.active) {
        // 'X' 아이콘
        COLORREF iconCol = dark ? RGB(210, 210, 210) : RGB(80, 80, 80);
        HPEN iconPen = CreatePen(PS_SOLID, std::max(1, ui::S(2)), iconCol);
        HGDIOBJ oldP = SelectObject(dc, iconPen);
        int sz = ui::S(4);
        MoveToEx(dc, cxA - sz, cyA - sz, nullptr);
        LineTo(dc, cxA + sz + 1, cyA + sz + 1);
        MoveToEx(dc, cxA + sz, cyA - sz, nullptr);
        LineTo(dc, cxA - sz - 1, cyA + sz + 1);
        SelectObject(dc, oldP);
        DeleteObject(iconPen);
    } else {
        // 돋보기 아이콘 (128x128 크기 기반)
        SearchIcons::DrawSearchIcon(dc, scratchDc, rcAction_, dark);
    }

    // 2. 필터/옵션 아이콘 (128x128 크기 기반)
    bool hasFilter = (state_.type != SearchType::All || state_.searchSubfolders || !state_.contentQuery.empty());
    SearchIcons::DrawFilterIcon(dc, scratchDc, rcFilter_, dark, hasFilter);

    if (scratchDc) DeleteDC(scratchDc);

    EndPaint(hwnd_, &ps);
}

void SearchBar::CommitSearch() {
    int len = GetWindowTextLengthW(edit_);
    std::wstring text(len + 1, L'\0');
    GetWindowTextW(edit_, text.data(), len + 1);
    text.resize(len);

    state_.ApplyGlobalOptions();

    // 파일명 입력값 없다 + 포함 문자 입력값 없다 상태에서 엔터키 입력 시 검색 종료 혹은 검색 안 함 처리
    if (text.empty() && state_.contentQuery.empty()) {
        if (state_.active) CancelSearch();
        return;
    }

    state_.query = text;
    state_.active = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (cb_.onSearch) cb_.onSearch(state_);
}

void SearchBar::CancelSearch() {
    state_.active = false;
    state_.query.clear();
    SetWindowTextW(edit_, L"");
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (cb_.onCancel) cb_.onCancel();
}

void SearchBar::Focus() {
    if (edit_) {
        SetFocus(edit_);
        SendMessageW(edit_, EM_SETSEL, 0, -1);
    }
}

void SearchBar::SetState(const TabSearchState& state) {
    state_ = state;
    state_.ApplyGlobalOptions();
    if (edit_) {
        SetWindowTextW(edit_, state_.query.c_str());
        if (state_.active) SendMessageW(edit_, EM_SETSEL, 0, -1);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SearchBar::OnPopupClosed() {
    popup_ = nullptr;
    popupClosedTick_ = GetTickCount64();
}

void SearchBar::ApplyTheme() {
    InvalidateRect(hwnd_, nullptr, TRUE);
    if (edit_) InvalidateRect(edit_, nullptr, TRUE);
}

void SearchBar::ApplyDpi() {
    if (edit_) SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(ui::font), TRUE);
    LayoutChildren();
}

void SearchBar::SetBounds(int x, int y, int w, int h) {
    MoveWindow(hwnd_, x, y, w, h, TRUE);
}

void SearchBar::ToggleFilterPopup() {
    if (popup_) {
        DestroyWindow(popup_);
        popup_ = nullptr;
        popupClosedTick_ = GetTickCount64();
        return;
    }

    if (GetTickCount64() - popupClosedTick_ < 250) {
        return;
    }

    state_.ApplyGlobalOptions();
    POINT pt{rcFilter_.right, rcFilter_.bottom + ui::S(4)};
    ClientToScreen(hwnd_, &pt);

    int popupW = ui::S(260);
    int popupH = ui::S(148);
    int x = pt.x - popupW;
    int y = pt.y;

    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof mi};
    if (GetMonitorInfoW(hMon, &mi)) {
        if (x < mi.rcWork.left) x = mi.rcWork.left;
        if (y + popupH > mi.rcWork.bottom) y = pt.y - (rcFilter_.bottom - rcFilter_.top) - popupH - ui::S(8);
    }

    popup_ = FilterPopup::Create(hwnd_, this, state_);
    if (popup_) {
        MoveWindow(popup_, x, y, popupW, popupH, FALSE);
        ShowWindow(popup_, SW_SHOW);
    }
}

}  // namespace ft
