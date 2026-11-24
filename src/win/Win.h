// Windows 전용 공통 헤더: 시스템 헤더, 사용자 정의 메시지, 색상/DPI/글꼴, 그리기 도우미
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <objbase.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/FileEntry.h"
#include "../core/NavigationHistory.h"
#include "../core/PathRules.h"
#include "../core/Settings.h"
#include "../core/Text.h"

namespace ft {

// ---- 사용자 정의 창 메시지 (모두 메인 창으로 전달) ----
enum : UINT {
    WM_FT_DIRREAD = WM_APP + 1,   // lParam = DirResult* (수신자가 소유)
    WM_FT_FSCHANGE,               // wParam = 감시 세대, lParam = 감시 창 HWND
    WM_FT_IMAGES,                 // 새 이미지가 준비됨
    WM_FT_OPDONE,                 // lParam = OpResult* (수신자가 소유)
};

enum class FileAction { Open, Copy, Paste, Delete, Rename, PermanentDelete };

struct OpResult {
    bool ok = false;
    std::wstring error;
};

extern HWND g_mainWnd;   // 메인 창(비동기 작업 완료 메시지 수신)

namespace ui {

// ---- 색상 (동적 테마 지원) ----
extern COLORREF kWindowBg;
extern COLORREF kPanelBg;
extern COLORREF kStripBg;
extern COLORREF kFieldBg;
extern COLORREF kFieldBorder;
extern COLORREF kBorder;
extern COLORREF kBorderIdle;
extern COLORREF kAccent;
extern COLORREF kText;
extern COLORREF kTextDim;
extern COLORREF kStatusText;
extern COLORREF kButtonBg;
extern COLORREF kButtonBorder;
extern COLORREF kHover;
extern COLORREF kPressed;
extern COLORREF kRowAlt;
extern COLORREF kSelectFill;
extern COLORREF kSelectBorder;
extern COLORREF kSelectFillInactive;
extern COLORREF kHeaderBg;

void SetDarkMode(bool dark);
bool IsDarkMode();
void ApplyWindowTheme(HWND hwnd);

// ---- DPI / 글꼴 (프로세스는 PerMonitorV2, 모든 창이 같은 DPI 를 쓴다고 가정) ----
extern UINT dpi;
extern HFONT font;
extern HFONT fontBold;
inline int S(int v) { return MulDiv(v, static_cast<int>(dpi), 96); }
void SetDpi(UINT newDpi);   // 글꼴을 다시 만든다

// ---- 그리기 도우미 ----
void FillRectColor(HDC dc, const RECT& rc, COLORREF color);
void FrameRectColor(HDC dc, const RECT& rc, COLORREF color, int thickness = 1);
void DrawTextIn(HDC dc, const std::wstring& text, RECT rc, UINT flags, COLORREF color, HFONT f = nullptr);
int  TextWidth(HDC dc, const std::wstring& text, HFONT f = nullptr);
int  TextHeight(HDC dc, HFONT f = nullptr);
inline bool PtIn(const RECT& rc, int x, int y) { POINT p{x, y}; return PtInRect(&rc, p) != FALSE; }
inline int RcW(const RECT& rc) { return rc.right - rc.left; }
inline int RcH(const RECT& rc) { return rc.bottom - rc.top; }

std::wstring GetText(HWND hwnd);
void ShowError(HWND owner, const std::wstring& message, const wchar_t* title = L"FileTab");
bool Confirm(HWND owner, const std::wstring& message, const wchar_t* title);

}  // namespace ui

// 260자를 넘는 경로를 위해 \\?\ 접두사를 붙인다(이미 있거나 짧으면 그대로).
std::wstring LongPath(const std::wstring& path);

// 환경 변수 확장(PathRules 훅용)
std::wstring ExpandEnv(const std::wstring& s);
std::wstring KnownFolder(const GUID& id);          // SHGetKnownFolderPath 래퍼
std::wstring FormatWin32Error(DWORD code);
std::wstring FormatModified(int64_t fileTime);     // "yyyy-MM-dd HH:mm" (지역 시간)

}  // namespace ft
