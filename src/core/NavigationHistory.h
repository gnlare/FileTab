// 탭별 뒤로/앞으로 이력. 폴더 조회가 성공했을 때만 호출자가 변경한다.
#pragma once
#include <string>
#include <vector>

#include "Text.h"

namespace ft {

struct FolderViewState {
    int scrollY = 0;
    std::vector<std::wstring> selection;
    std::wstring focus;
};

enum class SearchType : int {
    All = 0,         // 모든 폴더와 파일
    FilesOnly = 1,   // 모든 파일
    Images = 2,      // 이미지
    Videos = 3       // 영상
};

struct SearchOptions {
    SearchType type = SearchType::All;
    bool searchSubfolders = false;
    std::wstring contentQuery;
};

inline SearchOptions& GlobalSearchOptions() {
    static SearchOptions s_opts;
    return s_opts;
}

struct TabSearchState {
    std::wstring query;
    SearchType type = SearchType::All;
    bool searchSubfolders = false;
    std::wstring contentQuery;
    bool active = false;

    void ApplyGlobalOptions() {
        const auto& g = GlobalSearchOptions();
        type = g.type;
        searchSubfolders = g.searchSubfolders;
        contentQuery = g.contentQuery;
    }

    bool operator==(const TabSearchState& o) const {
        return active == o.active && query == o.query && type == o.type &&
               searchSubfolders == o.searchSubfolders && contentQuery == o.contentQuery;
    }
    bool operator!=(const TabSearchState& o) const { return !(*this == o); }
};

class NavigationHistory {
public:
    struct Entry {
        std::wstring path;
        FolderViewState state;
        TabSearchState search;
    };

    const std::wstring* Current() const { return index_ >= 0 ? &entries_[static_cast<size_t>(index_)].path : nullptr; }
    const std::wstring* Back() const { return index_ > 0 ? &entries_[static_cast<size_t>(index_ - 1)].path : nullptr; }
    const std::wstring* Forward() const {
        return index_ + 1 < static_cast<int>(entries_.size()) ? &entries_[static_cast<size_t>(index_ + 1)].path : nullptr;
    }

    const Entry* CurrentEntry() const {
        return (index_ >= 0 && index_ < static_cast<int>(entries_.size())) ? &entries_[static_cast<size_t>(index_)] : nullptr;
    }
    const Entry* BackEntry() const {
        return index_ > 0 ? &entries_[static_cast<size_t>(index_ - 1)] : nullptr;
    }
    const Entry* ForwardEntry() const {
        return index_ + 1 < static_cast<int>(entries_.size()) ? &entries_[static_cast<size_t>(index_ + 1)] : nullptr;
    }

    const TabSearchState* CurrentSearch() const {
        return (index_ >= 0 && index_ < static_cast<int>(entries_.size())) ? &entries_[static_cast<size_t>(index_)].search : nullptr;
    }

    void Visit(const std::wstring& path, const TabSearchState& search = {}) {
        if (Current() && EqualsNoCase(*Current(), path) && CurrentSearch() && *CurrentSearch() == search) return;
        entries_.resize(static_cast<size_t>(index_ + 1));
        entries_.push_back({path, {}, search});
        index_ = static_cast<int>(entries_.size()) - 1;
    }
    bool GoBack(const std::wstring& expected) {
        if (!Back() || !EqualsNoCase(*Back(), expected)) return false;
        --index_;
        return true;
    }
    bool GoForward(const std::wstring& expected) {
        if (!Forward() || !EqualsNoCase(*Forward(), expected)) return false;
        ++index_;
        return true;
    }

    void SaveCurrentState(const FolderViewState& state) {
        if (index_ >= 0 && index_ < static_cast<int>(entries_.size())) {
            entries_[static_cast<size_t>(index_)].state = state;
        }
    }
    const FolderViewState* CurrentState() const {
        return (index_ >= 0 && index_ < static_cast<int>(entries_.size())) ? &entries_[static_cast<size_t>(index_)].state : nullptr;
    }

private:
    std::vector<Entry> entries_;
    int index_ = -1;
};

}  // namespace ft
