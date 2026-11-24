#include "Text.h"

#include <cwctype>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlwapi.h>
#endif

namespace ft {

namespace {
void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}
bool IsDigitChar(wchar_t c) { return c >= L'0' && c <= L'9'; }
}  // namespace

std::string ToUtf8(std::wstring_view w) {
    std::string out;
    out.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(w[i]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size()) {
                uint32_t lo = static_cast<uint32_t>(w[i + 1]);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    ++i;
                }
            }
        }
        AppendUtf8(out, cp);
    }
    return out;
}

std::wstring FromUtf8(std::string_view s) {
    std::wstring out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        int extra;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; extra = 1; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; extra = 2; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; extra = 3; }
        else { out.push_back(0xFFFD); ++i; continue; }
        if (extra > 0 && i + static_cast<size_t>(extra) >= s.size()) {
            out.push_back(0xFFFD);
            break;
        }
        for (int k = 1; k <= extra; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += static_cast<size_t>(extra) + 1;
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp >= 0x10000) {
                cp -= 0x10000;
                out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
                out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
                continue;
            }
        }
        out.push_back(static_cast<wchar_t>(cp));
    }
    return out;
}

wchar_t FoldChar(wchar_t c) {
    if (c < 128) return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + 32) : c;
#ifdef _WIN32
    return static_cast<wchar_t>(reinterpret_cast<uintptr_t>(CharLowerW(reinterpret_cast<LPWSTR>(static_cast<uintptr_t>(c)))));
#else
    return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
#endif
}

bool EqualsNoCase(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i] && FoldChar(a[i]) != FoldChar(b[i])) return false;
    return true;
}

bool StartsWithNoCase(std::wstring_view s, std::wstring_view prefix) {
    return s.size() >= prefix.size() && EqualsNoCase(s.substr(0, prefix.size()), prefix);
}

int CompareNoCase(std::wstring_view a, std::wstring_view b) {
    size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
        wchar_t x = FoldChar(a[i]), y = FoldChar(b[i]);
        if (x != y) return x < y ? -1 : 1;
    }
    if (a.size() == b.size()) return 0;
    return a.size() < b.size() ? -1 : 1;
}

std::wstring ToLower(std::wstring_view s) {
    std::wstring r(s);
    for (auto& c : r) c = FoldChar(c);
    return r;
}

std::wstring Trim(std::wstring_view s) {
    size_t b = 0, e = s.size();
    auto ws = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0x3000 || c == 0xA0; };
    while (b < e && ws(s[b])) ++b;
    while (e > b && ws(s[e - 1])) --e;
    return std::wstring(s.substr(b, e - b));
}

int NaturalCompareFallback(const std::wstring& a, const std::wstring& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (IsDigitChar(a[i]) && IsDigitChar(b[j])) {
            size_t si = i, sj = j;
            while (i < a.size() && IsDigitChar(a[i])) ++i;
            while (j < b.size() && IsDigitChar(b[j])) ++j;
            size_t zi = si, zj = sj;
            while (zi + 1 < i && a[zi] == L'0') ++zi;
            while (zj + 1 < j && b[zj] == L'0') ++zj;
            size_t li = i - zi, lj = j - zj;
            if (li != lj) return li < lj ? -1 : 1;
            int c = a.compare(zi, li, b, zj, lj);
            if (c != 0) return c < 0 ? -1 : 1;
        } else {
            wchar_t x = FoldChar(a[i]), y = FoldChar(b[j]);
            if (x != y) return x < y ? -1 : 1;
            ++i;
            ++j;
        }
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

int NaturalCompare(const std::wstring& a, const std::wstring& b) {
#ifdef _WIN32
    return StrCmpLogicalW(a.c_str(), b.c_str());
#else
    return NaturalCompareFallback(a, b);
#endif
}

uint64_t HashCombine(uint64_t h, uint64_t v) {
    // FNV-1a 스타일 64비트 혼합
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h *= 1099511628211ull;
    return h;
}

uint64_t HashNoCase(std::wstring_view s) {
    uint64_t h = 1469598103934665603ull;
    for (wchar_t c : s) {
        h ^= static_cast<uint64_t>(FoldChar(c));
        h *= 1099511628211ull;
    }
    return h;
}

std::wstring FormatThousands(int64_t v) {
    bool neg = v < 0;
    uint64_t u = neg ? static_cast<uint64_t>(-(v + 1)) + 1 : static_cast<uint64_t>(v);
    std::wstring digits = std::to_wstring(u);
    std::wstring out;
    int count = 0;
    for (size_t i = digits.size(); i-- > 0;) {
        out.push_back(digits[i]);
        if (++count % 3 == 0 && i > 0) out.push_back(L',');
    }
    if (neg) out.push_back(L'-');
    return std::wstring(out.rbegin(), out.rend());
}

}  // namespace ft
