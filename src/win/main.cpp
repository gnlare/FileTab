// FileTab (C++/Win32) 진입점
#include "MainWindow.h"
#include "SettingsService.h"

using namespace ft;

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    // 단일 스레드 아파트먼트 + OLE: 드래그 앤 드롭, 클립보드, Shell COM 객체에 필요
    if (FAILED(OleInitialize(nullptr))) {
        MessageBoxW(nullptr, L"OLE 를 초기화하지 못했습니다.", L"FileTab", MB_OK | MB_ICONERROR);
        return 1;
    }
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof icc;
    icc.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
    path::SetEnvExpander(&ExpandEnv);

    MainWindow* window = MainWindow::Create(instance, showCommand);
    if (!window) {
        MessageBoxW(nullptr, L"창을 만들지 못했습니다.", L"FileTab", MB_OK | MB_ICONERROR);
        return 1;
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (window->PreTranslate(msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    // 느린 Shell 호출 중인 작업 스레드를 기다리지 않고 바로 종료한다(설정은 이미 저장됨).
    ExitProcess(static_cast<UINT>(msg.wParam));
}
