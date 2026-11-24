#include "PathRules.h"

#include "Text.h"

namespace ft::path {

namespace {

EnvExpander g_expander = nullptr;

bool IsAlpha(wchar_t c) { return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z'); }

// '/' 가 이미 '\' 로 바뀐 문자열을 루트와 나머지로 나눈다. 절대 경로가 아니면 false.
bool SplitRoot(const std::wstring& s, std::wstring& root, std::wstring& rest) {
    if (s.size() >= 2 && s[0] == L'\\' && s[1] == L'\\') {
        if (s.size() >= 4 && s[2] == L'?' && s[3] == L'\\') {  // 확장 경로 접두사
            std::wstring t = s.substr(4);
            if (StartsWithNoCase(t, L"UNC\\")) return SplitRoot(L"\\\\" + t.substr(4), root, rest);
            if (t.size() >= 2 && t[1] == L':' && IsAlpha(t[0])) return SplitRoot(t, root, rest);
            return false;
        }
        size_t serverEnd = s.find(L'\\', 2);
        if (serverEnd == std::wstring::npos) return false;
        size_t shareStart = serverEnd + 1;
        size_t shareEnd = s.find(L'\\', shareStart);
        std::wstring server = s.substr(2, serverEnd - 2);
        std::wstring share = shareEnd == std::wstring::npos ? s.substr(shareStart) : s.substr(shareStart, shareEnd - shareStart);
        if (server.empty() || share.empty()) return false;
        root = L"\\\\" + server + L"\\" + share;
        rest = shareEnd == std::wstring::npos ? std::wstring() : s.substr(shareEnd + 1);
        return true;
    }
    if (s.size() >= 2 && s[1] == L':' && IsAlpha(s[0])) {
        root = s.substr(0, 2) + L"\\";
        size_t start = (s.size() >= 3 && s[2] == L'\\') ? 3 : 2;
        rest = s.substr(start);
        return true;
    }
    return false;
}

}  // namespace

void SetEnvExpander(EnvExpander fn) { g_expander = fn; }

bool TryNormalize(const std::wstring& input, const std::wstring& base, std::wstring& out, std::wstring* error) {
    auto fail = [&](const wchar_t* message) {
        if (error) *error = message;
        return false;
    };
    std::wstring s = Trim(input);
    while (!s.empty() && s.front() == L'"') s.erase(0, 1);
    while (!s.empty() && s.back() == L'"') s.pop_back();
    if (g_expander) s = g_expander(s);
    if (Trim(s).empty()) return fail(L"경로를 입력하세요.");
    if (s.size() == 2 && s[1] == L':') s += L'\\';
    for (auto& c : s)
        if (c == L'/') c = L'\\';
    for (wchar_t c : s)
        if (c == L'\0' || c == L'|' || c == L'<' || c == L'>' || c == L'"' || c == L'?' || c == L'*') {
            // '?' 는 \\?\ 접두사에서만 허용한다.
            if (c == L'?' && s.compare(0, 4, L"\\\\?\\") == 0) continue;
            return fail(L"경로에 사용할 수 없는 문자가 있습니다.");
        }

    std::wstring root, rest;
    if (!SplitRoot(s, root, rest)) {
        std::wstring combined;
        if (!s.empty() && s[0] == L'\\' && !(s.size() >= 2 && s[1] == L'\\')) {
            // "\foo" : 기준 경로의 루트에 붙인다.
            std::wstring baseRoot = RootOf(base);
            if (baseRoot.empty()) return fail(L"절대 경로를 입력하세요.");
            if (baseRoot.back() == L'\\') baseRoot.pop_back();
            combined = baseRoot + s;
        } else {
            if (base.empty()) return fail(L"절대 경로를 입력하세요.");
            combined = Combine(base, s);
        }
        if (!SplitRoot(combined, root, rest)) return fail(L"올바르지 않은 경로입니다.");
    }

    std::vector<std::wstring> segments;
    size_t pos = 0;
    while (pos <= rest.size()) {
        size_t e = rest.find(L'\\', pos);
        if (e == std::wstring::npos) e = rest.size();
        std::wstring seg = rest.substr(pos, e - pos);
        pos = e + 1;
        if (seg.empty() || seg == L".") continue;
        if (seg == L"..") {
            if (!segments.empty()) segments.pop_back();
            continue;
        }
        segments.push_back(seg);
    }
    out = root;
    for (const auto& seg : segments) {
        if (out.back() != L'\\') out += L'\\';
        out += seg;
    }
    return true;
}

std::wstring RootOf(const std::wstring& normalized) {
    std::wstring root, rest;
    if (normalized.empty() || !SplitRoot(normalized, root, rest)) return std::wstring();
    return root;
}

bool IsRoot(const std::wstring& normalized) {
    std::wstring root, rest;
    if (!SplitRoot(normalized, root, rest)) return false;
    return Trim(rest).empty() || rest == L"\\";
}

std::wstring Parent(const std::wstring& p) {
    std::wstring root = RootOf(p);
    if (root.empty() || p.size() <= root.size()) return std::wstring();
    size_t idx = p.find_last_of(L'\\');
    if (idx == std::wstring::npos) return std::wstring();
    if (idx < root.size()) return root;
    return p.substr(0, idx);
}

std::wstring Combine(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    if (name.empty()) return dir;
    if (dir.back() == L'\\' || dir.back() == L'/') return dir + name;
    return dir + L'\\' + name;
}

std::wstring FileName(const std::wstring& p) {
    size_t idx = p.find_last_of(L"\\/:");
    return idx == std::wstring::npos ? p : p.substr(idx + 1);
}

std::wstring Label(const std::wstring& p) {
    std::wstring trimmed = p;
    while (!trimmed.empty() && (trimmed.back() == L'\\' || trimmed.back() == L'/')) trimmed.pop_back();
    std::wstring name = FileName(trimmed);
    return name.empty() ? p : name;
}

std::wstring Extension(const std::wstring& name) {
    size_t idx = name.rfind(L'.');
    if (idx == std::wstring::npos || idx + 1 >= name.size()) return std::wstring();
    return name.substr(idx);
}

std::wstring StemOf(const std::wstring& name) {
    std::wstring ext = Extension(name);
    return name.substr(0, name.size() - ext.size());
}

bool IsSameOrDescendant(const std::wstring& candidate, const std::wstring& ancestor) {
    std::wstring c, a;
    if (!TryNormalize(candidate, std::wstring(), c) || !TryNormalize(ancestor, std::wstring(), a)) return false;
    if (EqualsNoCase(c, a)) return true;
    std::wstring prefix = a;
    if (prefix.back() != L'\\') prefix += L'\\';
    return StartsWithNoCase(c, prefix);
}

std::wstring ValidateName(const std::wstring& name) {
    const wchar_t* kInvalid = L"Windows에서 사용할 수 없는 이름입니다.";
    if (Trim(name).empty() || name == L"." || name == L".." || name.back() == L'.' || name.back() == L' ')
        return kInvalid;
    for (wchar_t c : name) {
        if (c < 32) return kInvalid;
        switch (c) {
            case L'<': case L'>': case L':': case L'"': case L'/': case L'\\': case L'|': case L'?': case L'*':
                return kInvalid;
            default: break;
        }
    }
    size_t dot = name.find(L'.');
    std::wstring stem = dot == std::wstring::npos ? name : name.substr(0, dot);
    while (!stem.empty() && stem.back() == L' ') stem.pop_back();
    static const wchar_t* kReserved[] = {L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$"};
    for (const wchar_t* r : kReserved)
        if (EqualsNoCase(stem, r)) return L"예약된 Windows 장치 이름입니다.";
    if (stem.size() == 4 && (StartsWithNoCase(stem, L"COM") || StartsWithNoCase(stem, L"LPT"))) {
        static const std::wstring kDigits = L"123456789\u00B9\u00B2\u00B3";
        if (kDigits.find(stem[3]) != std::wstring::npos) return L"예약된 Windows 장치 이름입니다.";
    }
    if (name.size() > 255) return L"이름은 255자 이내로 입력하세요.";
    return std::wstring();
}

std::wstring ValidateCopy(const std::wstring& source, const std::wstring& destinationDirectory, bool isDirectory) {
    if (isDirectory && IsSameOrDescendant(destinationDirectory, source))
        return L"폴더 자신 또는 하위 폴더에는 복사할 수 없습니다.";
    return std::wstring();
}

std::vector<Crumb> Breadcrumbs(const std::wstring& path) {
    std::vector<Crumb> parts;
    std::wstring root = RootOf(path);
    if (root.empty()) return parts;
    parts.push_back({root, root});
    std::wstring current = root;
    size_t pos = root.size();
    while (pos < path.size()) {
        size_t e = path.find(L'\\', pos);
        if (e == std::wstring::npos) e = path.size();
        std::wstring seg = path.substr(pos, e - pos);
        pos = e + 1;
        if (seg.empty()) continue;
        current = Combine(current, seg);
        parts.push_back({seg, current});
    }
    return parts;
}

}  // namespace ft::path
