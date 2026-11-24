#pragma once
#include "Text.h"
#include <vector>
#include <utility>

namespace ft {
inline std::vector<std::pair<size_t, size_t>> FindRanges(std::wstring_view name, std::wstring_view query) {
    std::vector<std::pair<size_t, size_t>> ranges;
    if (query.empty()) return ranges;
    const auto lower = ToLower(name), needle = ToLower(query);
    for (size_t pos = lower.find(needle); pos != std::wstring::npos;
         pos = lower.find(needle, pos + needle.size()))
        ranges.emplace_back(pos, needle.size());
    return ranges;
}
inline bool FindContains(std::wstring_view name, std::wstring_view query) {
    if (query.empty()) return false;
    return ToLower(name).find(ToLower(query)) != std::wstring::npos;
}
inline bool MatchesWildcard(std::wstring_view text, std::wstring_view pattern) {
    size_t ti = 0, pi = 0, starPi = std::wstring_view::npos, starTi = 0;
    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == L'?' || ToLower(std::wstring(1, text[ti])) == ToLower(std::wstring(1, pattern[pi])))) {
            ++ti;
            ++pi;
        } else if (pi < pattern.size() && pattern[pi] == L'*') {
            starPi = pi++;
            starTi = ti;
        } else if (starPi != std::wstring_view::npos) {
            pi = starPi + 1;
            ti = ++starTi;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == L'*') ++pi;
    return pi == pattern.size();
}
inline bool FindMatchesName(std::wstring_view name, std::wstring_view query) {
    if (query.empty()) return false;
    if (query.find_first_of(L"*?") != std::wstring_view::npos) {
        return MatchesWildcard(name, query);
    }
    return FindContains(name, query);
}
// Search the list in display order; prefix selection starts strictly below the original focus.
template<class NameAt>
int FindPrefixBelow(int count, int focus, std::wstring_view query, NameAt nameAt) {
    if (query.empty()) return -1;
    for (int i = (focus < 0 ? 0 : focus + 1); i < count; ++i)
        if (StartsWithNoCase(nameAt(i), query)) return i;
    return -1;
}
template<class NameAt>
int FindSingleKey(int count, int focus, std::wstring_view query, NameAt nameAt) {
    int target = FindPrefixBelow(count, focus, query, nameAt);
    return target >= 0 ? target : FindPrefixBelow(count, -1, query, nameAt);
}
inline int FindFirstMatch(const std::vector<int>& matches) {
    return matches.empty() ? -1 : matches.front();
}
inline int FindClosestBelow(const std::vector<int>& matches, int focus) {
    if (matches.empty()) return -1;
    if (focus < 0) return matches.front();
    for (int m : matches) {
        if (m >= focus) return m;
    }
    return matches.front();
}
inline int FindNextMatch(const std::vector<int>& matches, int focus, bool reverse) {
    if (matches.empty()) return -1;
    if (reverse) {
        for (auto it = matches.rbegin(); it != matches.rend(); ++it)
            if (*it < focus) return *it;
        return matches.back();
    }
    for (int i : matches) if (i > focus) return i;
    return matches.front();
}
} // namespace ft
