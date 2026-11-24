// 메인 창: 상단 바, 단일/좌우 분할 레이아웃, 전역 단축키, 파일 작업 실행, 설정 저장
#pragma once
#include "PaneView.h"
#include "SearchBar.h"

namespace ft {

class MainWindow {
public:
    static MainWindow* Create(HINSTANCE instance, int showCommand);
    HWND hwnd() const { return hwnd_; }
    void ApplyTheme();

    // 메시지 루프에서 TranslateMessage 전에 호출. 처리했으면 true.
    bool PreTranslate(const MSG& msg);

private:
    MainWindow() = default;
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateChildren();
    void ApplyFonts();
    void LayoutAll();
    void SetDual(bool enabled);
    void SetActive(PaneView* pane);
    void SyncMode();
    void SyncSearch();
    void RunAction(PaneView* pane, FileAction action);
    void OnOperationDone(OpResult* result);
    void RefreshVisible();
    void SaveState();
    bool OnClose();
    bool HandleKey(WPARAM key);
    PaneView* PaneFromWindow(HWND window) const;
    PaneView* ActivePane() const { return active_ ? active_ : left_.get(); }
    RECT PaneArea() const;
    RECT SplitterRect() const;

    HWND hwnd_ = nullptr;
    HWND titleLabel_ = nullptr, settingsBtn_ = nullptr, dualBtn_ = nullptr, modeLabel_ = nullptr, modeCombo_ = nullptr;
    std::unique_ptr<SearchBar> searchBar_;
    std::unique_ptr<PaneView> left_, right_;
    PaneView* active_ = nullptr;
    bool dual_ = false;
    bool busy_ = false;
    bool syncing_ = false;
    bool draggingSplitter_ = false;
    double leftFrac_ = 0.5;       // 좌우 분할 시 왼쪽 패널 비율
    HBRUSH whiteBrush_ = nullptr;
    UINT_PTR imageTimer_ = 0;
};

}  // namespace ft
