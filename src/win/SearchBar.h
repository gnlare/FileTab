#pragma once
#include "Win.h"

namespace ft {

class SearchBar {
public:
    struct Callbacks {
        std::function<void(const TabSearchState&)> onSearch;
        std::function<void()> onCancel;
    };

    static bool RegisterWindowClass(HINSTANCE instance);
    static SearchBar* Create(HWND parent, Callbacks cb);
    ~SearchBar();

    HWND hwnd() const { return hwnd_; }
    void Focus();
    void SetState(const TabSearchState& state);
    const TabSearchState& GetState() const { return state_; }
    void ApplyTheme();
    void ApplyDpi();
    void SetBounds(int x, int y, int w, int h);
    void OnPopupClosed();

    friend class FilterPopup;

private:
    SearchBar() = default;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void LayoutChildren();
    void Paint();
    void ToggleFilterPopup();
    void CommitSearch();
    void CancelSearch();

    HWND hwnd_ = nullptr;
    HWND edit_ = nullptr;
    HWND popup_ = nullptr;
    Callbacks cb_;
    TabSearchState state_;

    RECT rcAction_{};
    RECT rcFilter_{};
    bool hotAction_ = false;
    bool hotFilter_ = false;
    bool trackLeave_ = false;
    uint64_t popupClosedTick_ = 0;
};

}  // namespace ft

