#include "Win.h"
#include <dwmapi.h>

namespace ft {

HWND g_mainWnd = nullptr;

namespace ui {

COLORREF kWindowBg = RGB(0xE9, 0xEE, 0xF5);
COLORREF kPanelBg = RGB(0xFF, 0xFF, 0xFF);
COLORREF kStripBg = RGB(0xF0, 0xF3, 0xF8);
COLORREF kFieldBg = RGB(0xFA, 0xFB, 0xFD);
COLORREF kFieldBorder = RGB(0xCF, 0xD6, 0xE0);
COLORREF kBorder = RGB(0xCF, 0xD6, 0xE0);
COLORREF kBorderIdle = RGB(0xD5, 0xDC, 0xE6);
COLORREF kAccent = RGB(0x25, 0x63, 0xEB);
COLORREF kText = RGB(0x17, 0x20, 0x33);
COLORREF kTextDim = RGB(0x64, 0x74, 0x8B);
COLORREF kStatusText = RGB(0x33, 0x41, 0x55);
COLORREF kButtonBg = RGB(0xF4, 0xF6, 0xF9);
COLORREF kButtonBorder = RGB(0xD4, 0xDA, 0xE3);
COLORREF kHover = RGB(0xE2, 0xE8, 0xF0);
COLORREF kPressed = RGB(0xCB, 0xD5, 0xE1);
COLORREF kRowAlt = RGB(0xF8, 0xFA, 0xFD);
COLORREF kSelectFill = RGB(0xCD, 0xE6, 0xFF);
COLORREF kSelectBorder = RGB(0x99, 0xD1, 0xFF);
COLORREF kSelectFillInactive = RGB(0xE6, 0xEA, 0xF0);
COLORREF kHeaderBg = RGB(0xF1, 0xF5, 0xF9);

static bool g_darkMode = false;

bool IsDarkMode() { return g_darkMode; }

void SetDarkMode(bool dark) {
    g_darkMode = dark;
    if (dark) {
        kWindowBg = RGB(0x18, 0x18, 0x18);
        kPanelBg = RGB(0x1F, 0x1F, 0x1F);
        kStripBg = RGB(0x25, 0x25, 0x26);
        kFieldBg = RGB(0x18, 0x18, 0x18);
        kFieldBorder = RGB(0x3E, 0x3E, 0x42);
        kBorder = RGB(0x3E, 0x3E, 0x42);
        kBorderIdle = RGB(0x3E, 0x3E, 0x42);
        kAccent = RGB(0x3B, 0x82, 0xF6);
        kText = RGB(0xDC, 0xDC, 0xDC);
        kTextDim = RGB(0x85, 0x85, 0x85);
        kStatusText = RGB(0xA0, 0xA0, 0xA0);
        kButtonBg = RGB(0x2D, 0x2D, 0x30);
        kButtonBorder = RGB(0x3E, 0x3E, 0x42);
        kHover = RGB(0x3E, 0x3E, 0x42);
        kPressed = RGB(0x0E, 0x63, 0x9C);
        kRowAlt = RGB(0x25, 0x25, 0x28);
        kSelectFill = RGB(0x09, 0x47, 0x71);
        kSelectBorder = RGB(0x00, 0x7A, 0xCC);
        kSelectFillInactive = RGB(0x37, 0x37, 0x3D);
        kHeaderBg = RGB(0x25, 0x25, 0x26);
    } else {
        kWindowBg = RGB(0xE9, 0xEE, 0xF5);
        kPanelBg = RGB(0xFF, 0xFF, 0xFF);
        kStripBg = RGB(0xF0, 0xF3, 0xF8);
        kFieldBg = RGB(0xFA, 0xFB, 0xFD);
        kFieldBorder = RGB(0xCF, 0xD6, 0xE0);
        kBorder = RGB(0xCF, 0xD6, 0xE0);
        kBorderIdle = RGB(0xD5, 0xDC, 0xE6);
        kAccent = RGB(0x25, 0x63, 0xEB);
        kText = RGB(0x17, 0x20, 0x33);
        kTextDim = RGB(0x64, 0x74, 0x8B);
        kStatusText = RGB(0x33, 0x41, 0x55);
        kButtonBg = RGB(0xF4, 0xF6, 0xF9);
        kButtonBorder = RGB(0xD4, 0xDA, 0xE3);
        kHover = RGB(0xE2, 0xE8, 0xF0);
        kPressed = RGB(0xCB, 0xD5, 0xE1);
        kRowAlt = RGB(0xF8, 0xFA, 0xFD);
        kSelectFill = RGB(0xCD, 0xE6, 0xFF);
        kSelectBorder = RGB(0x99, 0xD1, 0xFF);
        kSelectFillInactive = RGB(0xE6, 0xEA, 0xF0);
        kHeaderBg = RGB(0xF1, 0xF5, 0xF9);
    }
}

void ApplyWindowTheme(HWND hwnd) {
    if (!hwnd) return;
    BOOL useDark = g_darkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark));
    DwmSetWindowAttribute(hwnd, 19, &useDark, sizeof(useDark));
}

UINT dpi = 96;
HFONT font = nullptr;
HFONT fontBold = nullptr;

void SetDpi(UINT newDpi) {
    dpi = newDpi ? newDpi : 96;
    // 이전 글꼴은 아직 다른 창이 쓰고 있을 수 있으므로 지우지 않는다(DPI 변경은 드물고 글꼴은 작다).
    font = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    fontBold = CreateFontW(-S(12), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void FillRectColor(HDC dc, const RECT& rc, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
}

void FrameRectColor(HDC dc, const RECT& rc, COLORREF color, int thickness) {
    RECT r;
    r = {rc.left, rc.top, rc.right, rc.top + thickness};
    FillRectColor(dc, r, color);
    r = {rc.left, rc.bottom - thickness, rc.right, rc.bottom};
    FillRectColor(dc, r, color);
    r = {rc.left, rc.top + thickness, rc.left + thickness, rc.bottom - thickness};
    FillRectColor(dc, r, color);
    r = {rc.right - thickness, rc.top + thickness, rc.right, rc.bottom - thickness};
    FillRectColor(dc, r, color);
}

void DrawTextIn(HDC dc, const std::wstring& text, RECT rc, UINT flags, COLORREF color, HFONT f) {
    HGDIOBJ old = SelectObject(dc, f ? f : font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rc, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}

int TextWidth(HDC dc, const std::wstring& text, HFONT f) {
    HGDIOBJ old = SelectObject(dc, f ? f : font);
    SIZE sz{0, 0};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &sz);
    SelectObject(dc, old);
    return sz.cx;
}

int TextHeight(HDC dc, HFONT f) {
    HGDIOBJ old = SelectObject(dc, f ? f : font);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    return tm.tmHeight;
}

std::wstring GetText(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    std::wstring s(static_cast<size_t>(len) + 1, L'\0');
    int got = GetWindowTextW(hwnd, s.data(), len + 1);
    s.resize(static_cast<size_t>(got > 0 ? got : 0));
    return s;
}

void ShowError(HWND owner, const std::wstring& message, const wchar_t* title) {
    MessageBoxW(owner, message.c_str(), title, MB_OK | MB_ICONWARNING);
}

bool Confirm(HWND owner, const std::wstring& message, const wchar_t* title) {
    return MessageBoxW(owner, message.c_str(), title, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

}  // namespace ui

std::wstring LongPath(const std::wstring& path) {
    if (path.size() < 248 || path.compare(0, 4, L"\\\\?\\") == 0) return path;
    if (path.compare(0, 2, L"\\\\") == 0) return L"\\\\?\\UNC\\" + path.substr(2);
    return L"\\\\?\\" + path;
}

std::wstring ExpandEnv(const std::wstring& s) {
    if (s.find(L'%') == std::wstring::npos) return s;
    DWORD need = ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
    if (need == 0) return s;
    std::wstring out(need, L'\0');
    DWORD got = ExpandEnvironmentStringsW(s.c_str(), out.data(), need);
    if (got == 0) return s;
    out.resize(got - 1);
    return out;
}

std::wstring KnownFolder(const GUID& id) {
    PWSTR p = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) result = p;
    if (p) CoTaskMemFree(p);
    return result;
}

std::wstring FormatWin32Error(DWORD code) {
    wchar_t buf[512];
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, buf, 512, nullptr);
    std::wstring s(buf, n);
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ' || s.back() == L'.')) s.pop_back();
    if (s.empty()) s = L"오류 코드 " + std::to_wstring(code);
    return s;
}

std::wstring FormatModified(int64_t fileTime) {
    if (fileTime <= 0) return L"—";
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(static_cast<uint64_t>(fileTime) & 0xFFFFFFFFu);
    ft.dwHighDateTime = static_cast<DWORD>(static_cast<uint64_t>(fileTime) >> 32);
    SYSTEMTIME utc, local;
    if (!FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return L"—";
    return FormatDateTimeParts(local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute);
}

}  // namespace ft
