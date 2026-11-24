#include "FileEntry.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>

#include "Text.h"

namespace ft {

namespace {
struct ExtTable {
    std::mutex mutex;
    std::unordered_map<std::wstring, uint32_t> ids;
    std::vector<std::wstring> names{std::wstring()};  // 0번 = 확장자 없음
};
ExtTable& Table() {
    static ExtTable table;
    return table;
}

std::wstring Fmt2(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return std::wstring(s.begin(), s.end());
}
}  // namespace

uint32_t InternExtension(std::wstring_view ext) {
    if (ext.empty()) return 0;
    auto& t = Table();
    std::lock_guard<std::mutex> lock(t.mutex);
    std::wstring key(ext);
    auto it = t.ids.find(key);
    if (it != t.ids.end()) return it->second;
    uint32_t id = static_cast<uint32_t>(t.names.size());
    t.names.push_back(key);
    t.ids.emplace(std::move(key), id);
    return id;
}

std::wstring ExtensionOfId(uint32_t id) {
    auto& t = Table();
    std::lock_guard<std::mutex> lock(t.mutex);
    return id < t.names.size() ? t.names[id] : std::wstring();
}

int CompareEntries(const FileEntry& a, const FileEntry& b, SortKey key, bool descending) {
    if (a.IsDir() != b.IsDir()) return a.IsDir() ? -1 : 1;
    int result = 0;
    switch (key) {
        case SortKey::Size:
            // 폴더끼리는 크기가 없으므로 동률, 파일끼리는 값 비교
            if (!a.IsDir()) result = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
            break;
        case SortKey::Modified:
            result = a.modified < b.modified ? -1 : (a.modified > b.modified ? 1 : 0);
            break;
        case SortKey::Name:
            result = NaturalCompare(a.name, b.name);
            break;
    }
    if (result == 0 && key != SortKey::Name) result = NaturalCompare(a.name, b.name);
    if (result > 0) result = 1;
    if (result < 0) result = -1;
    return descending ? -result : result;
}

void SortEntries(std::vector<FileEntry>& entries, SortKey key, bool descending) {
    std::stable_sort(entries.begin(), entries.end(), [&](const FileEntry& x, const FileEntry& y) {
        return CompareEntries(x, y, key, descending) < 0;
    });
}

bool SameListing(const std::vector<FileEntry>& a, const std::vector<FileEntry>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const FileEntry& x = a[i];
        const FileEntry& y = b[i];
        if (x.size != y.size || x.modified != y.modified || x.attrs != y.attrs || x.name != y.name) return false;
    }
    return true;
}

std::wstring FormatSize(int64_t bytes) {
    if (bytes < 0) return L"—";
    const double KB = 1024.0, MB = KB * 1024.0, GB = MB * 1024.0, TB = GB * 1024.0;
    double b = static_cast<double>(bytes);
    if (b >= TB) return Fmt2(b / TB) + L" TB";
    if (b >= GB) return Fmt2(b / GB) + L" GB";
    if (b >= MB) return Fmt2(b / MB) + L" MB";
    if (b >= KB) return Fmt2(b / KB) + L" KB";
    return FormatThousands(bytes) + L" B";
}

std::wstring FormatDateTimeParts(int year, int month, int day, int hour, int minute) {
    wchar_t buf[32];
    std::swprintf(buf, 32, L"%04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
    return buf;
}

}  // namespace ft
