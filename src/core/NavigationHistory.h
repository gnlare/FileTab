// 탭별 뒤로/앞으로 이력. 폴더 조회가 성공했을 때만 호출자가 변경한다.
#pragma once
#include <string>
#include <vector>

#include "Text.h"

namespace ft {

class NavigationHistory {
public:
    const std::wstring* Current() const { return index_ >= 0 ? &paths_[static_cast<size_t>(index_)] : nullptr; }
    const std::wstring* Back() const { return index_ > 0 ? &paths_[static_cast<size_t>(index_ - 1)] : nullptr; }
    const std::wstring* Forward() const {
        return index_ + 1 < static_cast<int>(paths_.size()) ? &paths_[static_cast<size_t>(index_ + 1)] : nullptr;
    }

    void Visit(const std::wstring& path) {
        if (Current() && EqualsNoCase(*Current(), path)) return;
        paths_.resize(static_cast<size_t>(index_ + 1));
        paths_.push_back(path);
        index_ = static_cast<int>(paths_.size()) - 1;
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

private:
    std::vector<std::wstring> paths_;
    int index_ = -1;
};

}  // namespace ft
