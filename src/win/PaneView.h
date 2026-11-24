// 패널 하나: 탭 줄, 뒤로/앞으로/위로, 주소(이동 경로) 줄, 북마크 줄, 파일 목록, 상태 줄.
// 파일 목록(FileView)을 뺀 나머지는 한 창에서 직접 그리고 영역별로 마우스 처리를 한다.
#pragma once
#include "FsServices.h"
#include "FileView.h"
#include "FolderTab.h"

namespace ft {

class PaneDropTarget;

class PaneView : public FileView::Host {
public:
    struct Callbacks {
        std::function<void(PaneView*)> activated;
        std::function<void(PaneView*, FileAction)> action;   // 열기/복사/붙여넣기/삭제/이름 바꾸기
        std::function<void()> modeChanged;                   // 선택 탭 또는 그 보기 모드가 바뀜
        std::function<void()> bookmarksChanged;              // 북마크가 바뀜(두 패널 모두 다시 그림)
        std::function<void()> searchChanged;                 // 탭의 검색 상태가 바뀜
    };

    static bool RegisterWindowClass(HINSTANCE instance);
    static PaneView* Create(HWND parent);
    ~PaneView();

    HWND hwnd() const { return hwnd_; }
    Callbacks cb;

    // ---- 탭 모델 ----
    std::shared_ptr<FolderTab> SelectedTab() const;
    int SelectedIndex() const { return selected_; }
    void NewTab();
    void CloseSelectedTab() { if (auto t = SelectedTab()) CloseTab(t); }
    void SelectTab(int index, bool force = false);
    void SelectNextTab(int step);
    void Restore(const PaneState& state);
    PaneState Capture() const;

    void ExecuteSearch(const TabSearchState& state);
    void CancelSearch();
    TabSearchState GetCurrentSearchState() const;

    // ---- 상태 ----
    void SetActive(bool active);
    void SetPaneVisible(bool visible);
    bool HandleFindKey(WPARAM key) { return files_ && files_->HandleFindKey(key); }
    bool IsFileViewFocused() const { return files_ && files_->HasFocus(); }
    void FocusFiles() { if (files_) files_->Focus(); }
    void ApplyDpi();               // DPI·여백 설정 변경 반영
    void ApplyTheme();
    void OnFsChange(uintptr_t generation);   // 메인 창이 WM_FT_FSCHANGE 를 전달
    void OnBookmarksChanged();
    void RepaintFiles() { if (files_) files_->RepaintNow(); }

    // ---- 동작 ----
    void Refresh();
    void EditAddress();
    void Up();
    void OpenSelected();
    void CreateFolder();
    void AddSelectedBookmarks();
    void AddCurrentBookmark();
    void LoadFolderGroup(const FolderGroup& group);
    std::vector<FileEntry> SelectedEntries() const { return files_ ? files_->SelectedEntries() : std::vector<FileEntry>(); }
    std::vector<std::wstring> SelectedPaths() const { return files_ ? files_->SelectedPaths() : std::vector<std::wstring>(); }
    std::wstring CurrentPath() const { auto tab = SelectedTab(); return tab ? tab->Path() : HomePath(); }
    void SelectAfterRefresh(const std::wstring& name);   // 새로 고친 뒤 항목 선택
    void Dispose();

    // ---- FileView::Host ----
    void FvActivate() override;
    void FvSelectionChanged() override;
    void FvOpen() override;
    void FvContextMenu(POINT screen) override;
    void FvSortRequested(SortKey key) override;
    void FvDropFiles(const std::vector<std::wstring>& paths, const std::wstring& targetDir, bool isMove) override;

    // 드롭 대상에서 사용
    bool IsOverBookmarkRow(POINT clientPt) const;
    void AddBookmarksFromPaths(const std::vector<std::wstring>& paths);

private:
    PaneView() = default;
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK AddressProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    enum class HitKind { None, Tab, TabList, Plus, Back, Forward, Up, Group, Star, Bookmark, Crumb, Address };
    struct Hit {
        HitKind kind = HitKind::None;
        int index = -1;
        bool operator==(const Hit& o) const { return kind == o.kind && index == o.index; }
        bool operator!=(const Hit& o) const { return !(*this == o); }
    };
    struct Crumb {
        RECT rc{};
        std::wstring label;
        std::wstring target;
        bool ellipsis = false;
    };

    // 모델
    void CloseTab(std::shared_ptr<FolderTab> tab);
    std::shared_ptr<FolderTab> MakeTab(const std::wstring& path, ViewMode mode);
    void OnTabChanged(FolderTab& tab, unsigned what);
    void NavigateSelected(const std::wstring& path);
    void SetModeOfSelected(ViewMode mode);
    void StartWatcher();
    void StopWatcher();
    std::wstring HomePath() const;

    // 배치 / 그리기
    void Layout();
    void EnsureTabLayout();
    void EnsureBookmarkLayout();
    void BuildBreadcrumbs();
    void EnsureTabVisible(int index);
    Hit HitTestPane(int x, int y);
    void Paint();
    void DrawButton(HDC dc, const RECT& rc, const wchar_t* label, bool enabled, bool hot, bool pressed);
    std::wstring TooltipFor(int x, int y);
    void InvalidateRectSafe(const RECT& rc) { InvalidateRect(hwnd_, &rc, FALSE); }

    // 입력
    void OnLButtonDown(int x, int y);
    void OnMouseMove(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnRButtonUp(int x, int y);
    void Click(const Hit& hit);
    void ExitAddress();
    void CommitAddress();
    void ShowFilesMenu(POINT screen);
    void ShowTabListMenu();
    void ShowFolderGroupMenu();
    void OnGroupMenuCommand(int index);
    void OnGroupMenuRightClick(int index);
    void EditBookmark(int index);
    void DeleteBookmark(int index);
    void AddFolderGroupFromCurrent();
    void ReportOpenFailure(const std::wstring& path);

    HWND hwnd_ = nullptr;
    HWND edit_ = nullptr;
    HWND tip_ = nullptr;
    HMENU groupMenu_ = nullptr;
    std::unique_ptr<FileView> files_;
    PaneDropTarget* dropTarget_ = nullptr;
    bool active_ = false;
    bool visible_ = true;
    bool disposed_ = false;

    std::vector<std::shared_ptr<FolderTab>> tabs_;
    int selected_ = -1;
    std::wstring watchedPath_;
    DirWatcher watcher_;
    uintptr_t watchGen_ = 0;
    bool watchPending_ = false;
    bool refreshTimerOn_ = false;

    // 배치 결과
    RECT tabStrip_{}, tabArea_{}, tabListBtn_{}, plusBtn_{}, backBtn_{}, fwdBtn_{}, upBtn_{}, addrRect_{}, bmRow_{}, groupBtn_{},
        starBtn_{}, bmArea_{}, statusRect_{}, fileRect_{};
    std::vector<RECT> tabRects_;
    std::vector<RECT> bmRects_;
    std::vector<Crumb> crumbs_;
    int tabScroll_ = 0, bmScroll_ = 0;
    int tabsTotal_ = 0, bmTotal_ = 0;
    bool tabsDirty_ = true, bmDirty_ = true, crumbsDirty_ = true;
    bool editing_ = false;
    bool leaveTracked_ = false;

    // 마우스 상태
    Hit hot_, pressed_;
    int tabDrag_ = -1;
    int tabDownX_ = 0;
    bool tabDragging_ = false;

    // 그리기 자원
    HDC memDc_ = nullptr;
    HBITMAP backBmp_ = nullptr, backOld_ = nullptr;
    int backW_ = 0, backH_ = 0;
    std::wstring tipText_;
};

}  // namespace ft
