// 탭 하나의 상태: 현재 경로, 이력, 보기 모드, 정렬, 항목 목록, 비동기 폴더 읽기.
#pragma once
#include <map>
#include <optional>

#include "Win.h"

namespace ft {

class FolderTab;

// 워커 스레드가 만든 폴더 읽기 결과. WM_FT_DIRREAD 로 UI 스레드에 전달된다.
struct DirResult {
    std::weak_ptr<FolderTab> tab;
    uint64_t seq = 0;
    int direction = 0;
    std::wstring path;
    std::wstring childName;
    TabSearchState search;
    bool isSearch = false;
    bool ok = false;
    std::wstring error;
    std::vector<FileEntry> entries;
    SortKey sortKey = SortKey::Name;
    bool sortDesc = false;
};

enum TabChange : unsigned {
    kChangeEntries = 1,   // 목록이 바뀜
    kChangePath = 2,      // 경로(제목·이력)가 바뀜
    kChangeMode = 4,
    kChangeBusy = 8,      // 읽는 중 / 상태 문구
    kChangeSort = 16,
};

class FolderTab : public std::enable_shared_from_this<FolderTab> {
public:
    using Done = std::function<void(bool)>;
    using Listener = std::function<void(FolderTab&, unsigned)>;
    using StateProvider = std::function<void(FolderViewState& out)>;

    FolderTab(std::wstring path, ViewMode mode);

    const std::wstring& Path() const { return path_; }
    std::wstring Title() const { return path::Label(path_); }
    ViewMode Mode() const { return mode_; }
    void SetMode(ViewMode mode);
    bool CanBack() const { return history_.Back() != nullptr; }
    bool CanForward() const { return history_.Forward() != nullptr; }
    bool IsBusy() const { return busy_; }
    const std::wstring& Status() const { return status_; }
    std::shared_ptr<const std::vector<FileEntry>> Entries() const { return entries_; }
    SortKey SortColumn() const { return sortKey_; }
    bool SortDescending() const { return sortDesc_; }
    void Sort(SortKey key, bool descending);

    // 폴더를 읽어 성공하면 경로·이력·목록을 함께 바꾼다. 실패하면 기존 상태를 보존하고 done(false).
    void Navigate(const std::wstring& path, Done done = nullptr);
    void Refresh(Done done = nullptr);
    void Back(Done done = nullptr);
    void Forward(Done done = nullptr);
    void Up(Done done = nullptr);
    void Dispose();

    TabSearchState GetSearchState() const {
        TabSearchState st = searchState_;
        st.ApplyGlobalOptions();
        return st;
    }
    bool IsSearchActive() const { return searchState_.active; }
    void ExecuteSearch(const TabSearchState& state);
    void CancelSearch();

    void SetListener(Listener l) { listener_ = std::move(l); }
    void SetStateProvider(StateProvider p) { stateProvider_ = std::move(p); }
    void OnResult(DirResult& result);   // UI 스레드

    COLORREF Color() const { return color_; }
    void SetColor(COLORREF c) { color_ = (c == 0 ? CLR_INVALID : c); }
    bool HasColor() const { return color_ != CLR_INVALID && color_ != 0; }

    bool HasPendingRestore() const { return pendingRestoreState_.has_value(); }
    FolderViewState TakePendingRestore() {
        auto s = *pendingRestoreState_;
        pendingRestoreState_.reset();
        return s;
    }

    void SaveViewState(const std::wstring& path, const FolderViewState& state);
    bool GetViewState(const std::wstring& path, FolderViewState& out) const;

    // 탭을 떠났다가 돌아올 때 복원할 보기 상태
    std::vector<std::wstring> savedSelection;
    std::wstring savedFocus;
    int savedScroll = 0;

private:
    void Load(const std::wstring& input, int direction, Done done, const std::wstring& childName);
    void Load(const std::wstring& input, int direction, Done done, const std::wstring& childName, const TabSearchState& search);
    void Load(const std::wstring& input, int direction, Done done) { Load(input, direction, std::move(done), L"", {}); }
    void Finish(bool ok);
    void Notify(unsigned what) {
        if (listener_) listener_(*this, what);
    }

    struct PathCompareNoCase {
        bool operator()(std::wstring_view a, std::wstring_view b) const {
            return CompareNoCase(a, b) < 0;
        }
    };

    std::wstring path_;
    ViewMode mode_;
    NavigationHistory history_;
    SortKey sortKey_ = SortKey::Name;
    bool sortDesc_ = false;
    std::shared_ptr<const std::vector<FileEntry>> entries_;
    bool busy_ = false;
    bool disposed_ = false;
    std::wstring status_;
    uint64_t seq_ = 0;
    std::shared_ptr<std::atomic<bool>> cancel_;
    Done pendingDone_;
    Listener listener_;
    StateProvider stateProvider_;
    std::map<std::wstring, FolderViewState, PathCompareNoCase> pathStates_;
    std::optional<FolderViewState> pendingRestoreState_;
    TabSearchState searchState_;
    std::shared_ptr<const std::vector<FileEntry>> normalEntries_;
    COLORREF color_ = CLR_INVALID;
};

// 메인 창이 WM_FT_DIRREAD 를 받았을 때 호출한다(결과의 소유권을 가져간다).
void DispatchDirResult(DirResult* result);

}  // namespace ft
