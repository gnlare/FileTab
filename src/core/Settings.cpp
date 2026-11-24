#include "Settings.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>

#include "Json.h"
#include "Text.h"

namespace ft {

ViewMode ClampViewMode(int value) {
    switch (value) {
        case 1: return ViewMode::LargeIcons;
        case 2: return ViewMode::MediumIcons;
        default: return ViewMode::Details;
    }
}

namespace {

std::wstring StringOf(const Json* j) { return (j && j->IsString()) ? FromUtf8(j->AsString()) : std::wstring(); }

std::optional<double> OptionalNumber(const Json* j) {
    // The caller converts window dimensions to int pixels; reject extreme finite values too.
    if (j && j->IsNumber() && std::isfinite(j->AsNumber()) && j->AsNumber() >= 0 && j->AsNumber() <= 100000)
        return j->AsNumber();
    return std::nullopt;
}

PaneState ReadPane(const Json* j) {
    PaneState pane;
    if (!j || !j->IsObject()) return pane;
    if (const Json* tabs = j->Find("Tabs"); tabs && tabs->IsArray()) {
        for (const Json& t : tabs->Items()) {
            if (!t.IsObject()) continue;
            TabState ts;
            ts.path = StringOf(t.Find("Path"));
            if (const Json* m = t.Find("Mode"); m && m->IsNumber()) ts.mode = ClampViewMode(m->AsInt());
            pane.tabs.push_back(std::move(ts));
        }
    }
    if (const Json* idx = j->Find("SelectedIndex"); idx && idx->IsNumber()) pane.selectedIndex = idx->AsInt();
    return pane;
}

IconMargins ReadMargins(const Json* j) {
    IconMargins m;
    if (!j || !j->IsObject()) return m;
    // Out-of-range margins can overflow the Win32 tile size and scroll arithmetic.
    if (const Json* v = j->Find("Vertical"); v && v->IsNumber()) m.vertical = std::clamp(v->AsInt(10), 0, 200);
    if (const Json* h = j->Find("Horizontal"); h && h->IsNumber()) m.horizontal = std::clamp(h->AsInt(16), 0, 200);
    return m;
}

Json WritePane(const PaneState& pane) {
    Json o = Json::MakeObject();
    Json tabs = Json::MakeArray();
    for (const auto& t : pane.tabs) {
        Json to = Json::MakeObject();
        to.Set("Path", Json::MakeString(ToUtf8(t.path)));
        to.Set("Mode", Json::MakeNumber(static_cast<int>(t.mode)));
        tabs.Add(std::move(to));
    }
    o.Set("Tabs", std::move(tabs));
    o.Set("SelectedIndex", Json::MakeNumber(pane.selectedIndex));
    return o;
}

Json WriteMargins(const IconMargins& m) {
    Json o = Json::MakeObject();
    o.Set("Vertical", Json::MakeNumber(m.vertical));
    o.Set("Horizontal", Json::MakeNumber(m.horizontal));
    return o;
}

Json OptionalToJson(const std::optional<double>& v) { return v ? Json::MakeNumber(*v) : Json(); }

}  // namespace

AppSettings ParseSettings(std::string_view text) {
    Json root = ParseJson(text);
    AppSettings s;
    if (!root.IsObject()) return s;

    if (const Json* bs = root.Find("Bookmarks"); bs && bs->IsArray()) {
        for (const Json& b : bs->Items()) {
            if (!b.IsObject()) continue;
            Bookmark bm{StringOf(b.Find("Name")), StringOf(b.Find("Path"))};
            if (Trim(bm.path).empty()) continue;
            s.bookmarks.push_back(std::move(bm));
        }
    }
    if (const Json* gs = root.Find("FolderGroups"); gs && gs->IsArray()) {
        for (const Json& g : gs->Items()) {
            if (!g.IsObject()) continue;
            FolderGroup fg;
            fg.name = StringOf(g.Find("Name"));
            if (Trim(fg.name).empty()) continue;
            if (const Json* ps = g.Find("Paths"); ps && ps->IsArray())
                for (const Json& p : ps->Items())
                    if (p.IsString()) fg.paths.push_back(FromUtf8(p.AsString()));
            if (const Json* m = g.Find("Mode"); m && m->IsNumber()) fg.mode = ClampViewMode(m->AsInt());
            if (const Json* ms = g.Find("Modes"); ms && ms->IsArray())
                for (const Json& m : ms->Items())
                    if (m.IsNumber()) fg.modes.push_back(ClampViewMode(m.AsInt()));
            if (const Json* cs = g.Find("Colors"); cs && cs->IsArray())
                for (const Json& c : cs->Items())
                    if (c.IsNumber()) {
                        const double raw = c.AsNumber();
                        if (!std::isfinite(raw) || raw < 0 ||
                            raw > static_cast<double>(std::numeric_limits<uint32_t>::max())) continue;
                        uint32_t val = static_cast<uint32_t>(raw);
                        fg.colors.push_back((val == 0 || val == 0xFFFFFFFF) ? 0xFFFFFFFF : val);
                    }
            s.folderGroups.push_back(std::move(fg));
        }
    }
    s.left = ReadPane(root.Find("Left"));
    s.right = ReadPane(root.Find("Right"));
    if (const Json* d = root.Find("DualPane")) s.dualPane = d->AsBool(false);
    s.largeIconMargins = ReadMargins(root.Find("LargeIconMargins"));
    s.mediumIconMargins = ReadMargins(root.Find("MediumIconMargins"));
    if (const Json* sc = root.Find("Shortcuts"); sc && sc->IsArray()) {
        for (const Json& item : sc->Items()) {
            if (!item.IsObject()) continue;
            const Json* idJson = item.Find("Id");
            std::string id = idJson && idJson->IsString() ? idJson->AsString() : "";
            std::wstring key = StringOf(item.Find("Key"));
            for (auto& def : s.shortcuts) {
                if (def.id == id) {
                    if (const Json* k = item.Find("Key"); k && k->IsString()) def.shortcut = key;
                    if (const Json* k = item.Find("Key2"); k && k->IsString()) def.shortcut2 = StringOf(k);
                    break;
                }
            }
        }
    }
    for (auto& item : s.shortcuts) {
        if (IsFixedShortcut(item.shortcut)) item.shortcut.clear();
        if (IsFixedShortcut(item.shortcut2)) item.shortcut2.clear();
    }
    s.windowWidth = OptionalNumber(root.Find("WindowWidth"));
    s.windowHeight = OptionalNumber(root.Find("WindowHeight"));
    s.dualWindowWidth = OptionalNumber(root.Find("DualWindowWidth"));
    s.leftPaneWidth = OptionalNumber(root.Find("LeftPaneWidth"));
    s.rightPaneWidth = OptionalNumber(root.Find("RightPaneWidth"));
    if (const Json* m = root.Find("IsMaximized")) s.isMaximized = m->AsBool(false);
    if (const Json* dm = root.Find("DarkMode")) s.darkMode = dm->AsBool(false);
    return s;
}

std::string SerializeSettings(const AppSettings& s) {
    Json root = Json::MakeObject();

    Json bookmarks = Json::MakeArray();
    for (const auto& b : s.bookmarks) {
        Json o = Json::MakeObject();
        o.Set("Name", Json::MakeString(ToUtf8(b.name)));
        o.Set("Path", Json::MakeString(ToUtf8(b.path)));
        bookmarks.Add(std::move(o));
    }
    root.Set("Bookmarks", std::move(bookmarks));

    Json groups = Json::MakeArray();
    for (const auto& g : s.folderGroups) {
        Json o = Json::MakeObject();
        o.Set("Name", Json::MakeString(ToUtf8(g.name)));
        Json paths = Json::MakeArray();
        for (const auto& p : g.paths) paths.Add(Json::MakeString(ToUtf8(p)));
        o.Set("Paths", std::move(paths));
        o.Set("Mode", Json::MakeNumber(static_cast<int>(g.mode)));
        Json modes = Json::MakeArray();
        for (ViewMode m : g.modes) modes.Add(Json::MakeNumber(static_cast<int>(m)));
        o.Set("Modes", std::move(modes));
        if (!g.colors.empty()) {
            Json colors = Json::MakeArray();
            for (uint32_t c : g.colors) colors.Add(Json::MakeNumber(static_cast<double>(c)));
            o.Set("Colors", std::move(colors));
        }
        groups.Add(std::move(o));
    }
    root.Set("FolderGroups", std::move(groups));

    root.Set("Left", WritePane(s.left));
    root.Set("Right", WritePane(s.right));
    root.Set("DualPane", Json::MakeBool(s.dualPane));
    root.Set("LargeIconMargins", WriteMargins(s.largeIconMargins));
    root.Set("MediumIconMargins", WriteMargins(s.mediumIconMargins));
    root.Set("WindowWidth", OptionalToJson(s.windowWidth));
    root.Set("WindowHeight", OptionalToJson(s.windowHeight));
    root.Set("DualWindowWidth", OptionalToJson(s.dualWindowWidth));
    root.Set("LeftPaneWidth", OptionalToJson(s.leftPaneWidth));
    root.Set("RightPaneWidth", OptionalToJson(s.rightPaneWidth));
    root.Set("IsMaximized", Json::MakeBool(s.isMaximized));
    root.Set("DarkMode", Json::MakeBool(s.darkMode));

    Json shortcuts = Json::MakeArray();
    for (const auto& sc : s.shortcuts) {
        Json o = Json::MakeObject();
        o.Set("Id", Json::MakeString(sc.id));
        o.Set("Key", Json::MakeString(ToUtf8(sc.shortcut)));
        o.Set("Key2", Json::MakeString(ToUtf8(sc.shortcut2)));
        shortcuts.Add(std::move(o));
    }
    root.Set("Shortcuts", std::move(shortcuts));

    return DumpJson(root, 2);
}

std::vector<ShortcutItem> DefaultShortcuts() {
    return {
        {"dual_pane",   L"좌우 분할 / 단일 창 전환", L"F6"},
        {"new_tab",     L"새 탭",                    L"Ctrl+T"},
        {"close_tab",   L"탭 닫기",                  L"Ctrl+W"},
        {"address",     L"주소창 입력",              L"Ctrl+L"},
        {"parent",      L"상위 폴더",                L"Alt+Up", L"Backspace"},
        {"back",        L"뒤로 이동",                L"Alt+Left"},
        {"forward",     L"앞으로 이동",              L"Alt+Right"},
        {"bookmark",    L"현재 폴더 북마크",          L"Ctrl+D"}
    };
}

// File commands are reserved even when legacy settings contain custom bindings.
namespace {
struct FixedKey { unsigned int key; bool ctrl; bool shift; };
constexpr FixedKey fixedKeys[] = {{0x74, false, false}, {'C', true, false},
    {'V', true, false}, {0x2E, false, false}, {0x2E, false, true}, {0x71, false, false}};
}
bool IsFixedShortcutKey(unsigned int key, bool ctrl, bool alt, bool shift) {
    if (alt) return false;
    for (const auto& k : fixedKeys)
        if (key == k.key && ctrl == k.ctrl && shift == k.shift) return true;
    return false;
}
bool IsFixedShortcut(const std::wstring& shortcut) {
    for (const auto& k : fixedKeys)
        if (MatchShortcut(shortcut, k.key, k.ctrl, false, k.shift)) return true;
    return false;
}

bool MatchShortcut(const std::wstring& shortcutStr, unsigned int key, bool ctrl, bool alt, bool shift) {
    if (shortcutStr.empty()) return false;
    bool reqCtrl = false, reqAlt = false, reqShift = false;
    std::wstring mainKey;

    size_t start = 0;
    while (start < shortcutStr.size()) {
        size_t plus = shortcutStr.find(L'+', start);
        if (plus == std::wstring::npos) plus = shortcutStr.size();
        std::wstring part = shortcutStr.substr(start, plus - start);
        start = plus + 1;
        while (!part.empty() && (part.front() == L' ' || part.front() == L'\t')) part.erase(part.begin());
        while (!part.empty() && (part.back() == L' ' || part.back() == L'\t')) part.pop_back();
        if (part.empty()) continue;

        std::wstring lower = part;
        for (auto& c : lower) c = towlower(c);

        if (lower == L"ctrl" || lower == L"control") reqCtrl = true;
        else if (lower == L"alt") reqAlt = true;
        else if (lower == L"shift") reqShift = true;
        else mainKey = lower;
    }

    if (ctrl != reqCtrl || alt != reqAlt || shift != reqShift) return false;
    if (mainKey.empty()) return false;

    // F1 - F12
    if (mainKey.size() >= 2 && mainKey[0] == L'f' && mainKey[1] >= L'1' && mainKey[1] <= L'9') {
        int fNum = 0;
        if (mainKey.size() == 2) fNum = mainKey[1] - L'0';
        else if (mainKey.size() == 3 && mainKey[1] == L'1' && mainKey[2] >= L'0' && mainKey[2] <= L'2')
            fNum = 10 + (mainKey[2] - L'0');
        if (fNum >= 1 && fNum <= 12) return key == static_cast<unsigned int>(0x70 + fNum - 1);
    }

    if (mainKey.rfind(L"vk", 0) == 0 && mainKey.size() > 2 && mainKey.size() <= 5) {
        unsigned int v = 0;
        for (size_t i = 2; i < mainKey.size(); ++i) {
            if (mainKey[i] < L'0' || mainKey[i] > L'9') return false;
            v = v * 10 + (mainKey[i] - L'0');
        }
        return v <= 255 && key == v;
    }
    if (mainKey == L"del" || mainKey == L"delete") return key == 0x2E;
    if (mainKey == L"backspace" || mainKey == L"back" || mainKey == L"bs") return key == 0x08;
    if (mainKey == L"enter" || mainKey == L"return") return key == 0x0D;
    if (mainKey == L"left") return key == 0x25;
    if (mainKey == L"up") return key == 0x26;
    if (mainKey == L"right") return key == 0x27;
    if (mainKey == L"down") return key == 0x28;
    if (mainKey == L"tab") return key == 0x09;
    if (mainKey == L"space") return key == 0x20;
    if (mainKey == L"esc" || mainKey == L"escape") return key == 0x1B;

    if (mainKey.size() == 1) {
        wchar_t c = towupper(mainKey[0]);
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) return key == static_cast<unsigned int>(c);
    }

    return false;
}

}  // namespace ft
