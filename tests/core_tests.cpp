// 플랫폼 독립 코어 테스트. Windows·Linux 어디서든 빌드해서 실행할 수 있다.
// FileTab.Core.Tests(C#)의 검사 항목을 그대로 옮기고, 정렬·JSON·설정·캐시 검사를 더했다.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../src/core/FileEntry.h"
#include "../src/core/GridNavigation.h"
#include "../src/core/QuickFind.h"
#include "../src/core/Json.h"
#include "../src/core/LruCache.h"
#include "../src/core/NavigationHistory.h"
#include "../src/core/PathRules.h"
#include "../src/core/Settings.h"
#include "../src/core/Text.h"

using namespace ft;

static int g_count = 0;
static int g_failed = 0;

#define CHECK(cond, desc)                                                       \
    do {                                                                        \
        ++g_count;                                                              \
        if (cond) {                                                             \
            std::printf("PASS: %s\n", desc);                                    \
        } else {                                                                \
            ++g_failed;                                                         \
            std::printf("FAIL: %s  (line %d)\n", desc, __LINE__);               \
        }                                                                       \
    } while (0)

static std::wstring Norm(const std::wstring& in, const std::wstring& base = L"") {
    std::wstring out;
    return path::TryNormalize(in, base, out) ? out : L"<fail>";
}

static void TestHistory() {
    NavigationHistory h;
    CHECK(!h.Current() && !h.Back() && !h.Forward(), "Empty history");
    h.Visit(L"A"); h.Visit(L"B"); h.Visit(L"C");
    CHECK(*h.Back() == L"B" && *h.Current() == L"C", "Visit commits a location");
    CHECK(!h.GoBack(L"A") && *h.Current() == L"C", "Stale navigation cannot move history");
    CHECK(h.GoBack(L"B") && *h.Forward() == L"C", "Back navigation");
    CHECK(h.GoForward(L"C") && *h.Back() == L"B", "Forward navigation");
    h.GoBack(L"B");
    h.Visit(L"D");
    CHECK(!h.Forward() && *h.Back() == L"B", "New navigation discards forward branch");
    h.Visit(L"d");
    CHECK(*h.Back() == L"B", "Duplicate visits do not grow history");

    // Test view state persistence across navigation
    h.SaveCurrentState({120, {L"file1", L"file2"}, L"file1"});
    CHECK(h.CurrentState() != nullptr && h.CurrentState()->scrollY == 120, "State saved on current entry");
    h.GoBack(L"B");
    h.SaveCurrentState({50, {L"dirB"}, L"dirB"});
    h.GoForward(L"D");
    CHECK(h.CurrentState() != nullptr && h.CurrentState()->scrollY == 120, "Forward restores entry state");
    CHECK(h.CurrentState()->selection.size() == 2 && h.CurrentState()->focus == L"file1", "Selection preserved");
    h.GoBack(L"B");
    CHECK(h.CurrentState() != nullptr && h.CurrentState()->scrollY == 50 && h.CurrentState()->focus == L"dirB", "Back restores entry state");

    // Test search state in history
    TabSearchState s1;
    s1.active = true;
    s1.query = L"needle";
    h.Visit(L"B", s1);
    CHECK(h.CurrentSearch() != nullptr && h.CurrentSearch()->active && h.CurrentSearch()->query == L"needle", "Search state visited");
    h.GoBack(L"B");
    CHECK(h.CurrentSearch() != nullptr && !h.CurrentSearch()->active, "Back restores non-search state");
    h.GoForward(L"B");
    CHECK(h.CurrentSearch() != nullptr && h.CurrentSearch()->active && h.CurrentSearch()->query == L"needle", "Forward restores search state");
}

static void TestPaths() {
    const std::wstring root = L"C:\\Temp\\filetab-core-test";
    CHECK(path::IsSameOrDescendant(root + L"\\child", root), "Descendant recognized");
    CHECK(path::IsSameOrDescendant(root, root), "Self recognized");
    CHECK(!path::IsSameOrDescendant(root + L"-sibling", root), "Sibling prefix is not a descendant");
    CHECK(Norm(L"child", root) == root + L"\\child", "Relative path uses current folder");
    CHECK(!path::ValidateCopy(root, root, true).empty(), "Reject self-recursive folder copy");
    CHECK(!path::ValidateCopy(root, root + L"\\child", true).empty(), "Reject copying folder into its child");
    CHECK(path::ValidateCopy(root, root + L"\\child", false).empty(), "Files may be copied anywhere");

    const wchar_t* bad[] = {L"", L".", L"..", L"CON", L"con.txt", L"NUL.log", L"COM1", L"LPT9.txt", L"LPT\u00B9",
                            L"bad?name", L"a/b", L"a\\b", L"bad.", L"bad ", L"a:b"};
    for (const wchar_t* n : bad) CHECK(!path::ValidateName(n).empty(), "Reject invalid name");
    const wchar_t* good[] = {L"보고서 1.txt", L"file.name", L"COM10", L"normal", L".gitignore"};
    for (const wchar_t* n : good) CHECK(path::ValidateName(n).empty(), "Accept valid name");

    CHECK(Norm(L"C:") == L"C:\\", "Drive-only input is absolute");
    CHECK(Norm(L"\\\\server\\share\\folder\\..") == Norm(L"\\\\server\\share\\"), "UNC normalization");
    CHECK(Norm(L"\\\\server\\share\\folder\\..") == L"\\\\server\\share", "UNC root has no trailing separator");
    CHECK(path::IsSameOrDescendant(L"C:\\folder\\child", L"c:\\FOLDER"), "Windows case-insensitive paths");
    CHECK(Norm(L"\"C:\\a\\b\\\"") == L"C:\\a\\b", "Quotes and trailing separator trimmed");
    CHECK(Norm(L"C:/a/./b/../c") == L"C:\\a\\c", "Forward slashes, dot and dotdot resolved");
    CHECK(Norm(L"C:\\..\\..") == L"C:\\", "Cannot climb above root");
    CHECK(Norm(L"\\x", L"D:\\foo\\bar") == L"D:\\x", "Rooted path uses base drive");
    CHECK(Norm(L"..", L"D:\\foo\\bar") == L"D:\\foo", "Relative parent");
    CHECK(Norm(L"rel") == L"<fail>", "Relative path without base is rejected");
    CHECK(Norm(L"   ") == L"<fail>", "Blank path is rejected");

    CHECK(path::Parent(L"C:\\a\\b") == L"C:\\a", "Parent of nested folder");
    CHECK(path::Parent(L"C:\\a") == L"C:\\", "Parent of top-level folder is the drive");
    CHECK(path::Parent(L"C:\\").empty(), "Drive root has no parent");
    CHECK(path::Parent(L"\\\\s\\sh\\x") == L"\\\\s\\sh", "UNC parent");
    CHECK(path::Parent(L"\\\\s\\sh").empty(), "UNC root has no parent");
    CHECK(path::Label(L"C:\\a\\b") == L"b" && path::Label(L"C:\\") == L"C:\\", "Tab labels");
    CHECK(path::Extension(L"a.tar.gz") == L".gz" && path::Extension(L"noext").empty() && path::Extension(L"x.").empty(),
          "Extension");
    CHECK(path::StemOf(L"a.tar.gz") == L"a.tar", "Stem");

    auto crumbs = path::Breadcrumbs(L"C:\\Users\\me\\Docs");
    CHECK(crumbs.size() == 4 && crumbs[0].label == L"C:\\" && crumbs[3].path == L"C:\\Users\\me\\Docs", "Breadcrumbs for drive path");
    auto unc = path::Breadcrumbs(L"\\\\srv\\share\\x");
    CHECK(unc.size() == 2 && unc[0].path == L"\\\\srv\\share" && unc[1].path == L"\\\\srv\\share\\x", "Breadcrumbs for UNC path");
}

static void TestText() {
    CHECK(ToUtf8(L"가a") == "\xEA\xB0\x80" "a", "UTF-8 encode");
    CHECK(FromUtf8("\xEA\xB0\x80" "a") == L"가a", "UTF-8 decode");
    CHECK(FromUtf8(ToUtf8(L"한글 😀 test")) == L"한글 😀 test", "UTF-8 round trip with astral plane");
    CHECK(NaturalCompareFallback(L"file2", L"file10") < 0, "Natural order: 2 before 10");
    CHECK(NaturalCompareFallback(L"File2", L"file2") == 0, "Natural order ignores case");
    CHECK(NaturalCompareFallback(L"a", L"b") < 0 && NaturalCompareFallback(L"b", L"a") > 0, "Natural order letters");
    CHECK(NaturalCompareFallback(L"a01", L"a1") == 0, "Leading zeros equal");
    CHECK(FormatThousands(1234567) == L"1,234,567" && FormatThousands(999) == L"999" && FormatThousands(0) == L"0",
          "Thousands separators");
    CHECK(EqualsNoCase(L"ABC", L"abc") && !EqualsNoCase(L"abc", L"abd"), "Case-insensitive equality");
}

static FileEntry E(const wchar_t* name, bool dir, uint64_t size, int64_t mod) {
    FileEntry e;
    e.name = name;
    e.attrs = dir ? kAttrDirectory : 0;
    e.size = size;
    e.modified = mod;
    return e;
}

static void TestEntries() {
    CHECK(FormatSize(0) == L"0 B" && FormatSize(1024) == L"1 KB" && FormatSize(1048576) == L"1 MB" &&
              FormatSize(1073741824) == L"1 GB" && FormatSize(1099511627776LL) == L"1 TB",
          "Size formatted in KB, MB, GB, TB");
    CHECK(FormatSize(1536) == L"1.5 KB" && FormatSize(1023) == L"1,023 B", "Size decimals and separators");
    CHECK(FormatSize(-1) == L"—", "Negative size placeholder");

    std::vector<FileEntry> v = {E(L"b.txt", false, 5, 10), E(L"a10", true, 0, 3), E(L"a2", true, 0, 7), E(L"a.txt", false, 100, 1)};
    SortEntries(v, SortKey::Name, false);
    CHECK(v[0].name == L"a2" && v[1].name == L"a10" && v[2].name == L"a.txt" && v[3].name == L"b.txt", "Name sort: folders first, natural order");
    SortEntries(v, SortKey::Name, true);
    CHECK(v[0].name == L"a10" && v[1].name == L"a2" && v[2].name == L"b.txt", "Descending keeps folders first");
    SortEntries(v, SortKey::Size, false);
    CHECK(v[2].name == L"b.txt" && v[3].name == L"a.txt", "Size sort");
    SortEntries(v, SortKey::Modified, false);
    CHECK(v[0].name == L"a10" && v[1].name == L"a2" && v[2].name == L"a.txt", "Modified sort");

    std::vector<FileEntry> w = v;
    CHECK(SameListing(v, w), "Identical listings compare equal");
    w[1].modified += 1;
    CHECK(!SameListing(v, w), "A changed timestamp is detected");
    w = v;
    w.pop_back();
    CHECK(!SameListing(v, w), "A removed entry is detected");

    uint32_t a = InternExtension(L".txt"), b = InternExtension(L".txt"), c = InternExtension(L".png");
    CHECK(a == b && a != c && a != 0 && InternExtension(L"") == 0 && ExtensionOfId(c) == L".png", "Extension interning");
}

static void TestJsonAndSettings() {
    Json j = ParseJson("{\"a\": [1, 2.5, -3e2, true, null, \"x\\u00e9\\n\\ud83d\\ude00\"], \"B\": {}}");
    CHECK(j.IsObject() && j.Find("a")->Items().size() == 6, "JSON parse array");
    CHECK(j.Find("a")->Items()[1].AsNumber() == 2.5 && j.Find("a")->Items()[2].AsNumber() == -300.0, "JSON numbers");
    CHECK(FromUtf8(j.Find("a")->Items()[5].AsString()) == L"x\u00e9\n😀", "JSON string escapes and surrogates");
    CHECK(j.Find("b") != nullptr, "JSON lookup falls back to case-insensitive match");
    CHECK(Json::MakeNumber(1e300).AsInt(42) == 42 && Json::MakeNumber(-1e300).AsInt(42) == 42,
          "Out-of-range JSON numbers do not overflow integers");
    bool threw = false;
    try { ParseJson("{\"a\": }"); } catch (const JsonError&) { threw = true; }
    CHECK(threw, "JSON syntax error throws");
    threw = false;
    try { ParseJson("{\"a\": 1} x"); } catch (const JsonError&) { threw = true; }
    CHECK(threw, "JSON trailing garbage throws");

    // System.Text.Json(C# 버전)이 쓰던 형태의 설정을 그대로 읽을 수 있어야 한다.
    const char* csharp =
        "{\n  \"Bookmarks\": [{\"Name\": \"me\", \"Path\": \"C:\\\\Users\\\\me\"}, {\"Name\": \"x\", \"Path\": \"\"}],\n"
        "  \"FolderGroups\": [{\"Name\": \"g\", \"Paths\": [\"C:\\\\a\", \"D:\\\\b\"], \"Mode\": 1, \"Modes\": [1, 2], \"Summary\": \"폴더 2개\"}],\n"
        "  \"Left\": {\"Tabs\": [{\"Path\": \"C:\\\\a\", \"Mode\": 2}, {\"Path\": \"D:\\\\\", \"Mode\": 9}], \"SelectedIndex\": 1},\n"
        "  \"Right\": {\"Tabs\": [], \"SelectedIndex\": 0},\n"
        "  \"DualPane\": true,\n  \"LargeIconMargins\": {\"Vertical\": 12, \"Horizontal\": 20},\n"
        "  \"MediumIconMargins\": {\"Vertical\": 10, \"Horizontal\": 16},\n"
        "  \"WindowWidth\": 1280.5, \"WindowHeight\": null, \"DualWindowWidth\": 1500, \"LeftPaneWidth\": 700, \"RightPaneWidth\": 800,\n"
        "  \"IsMaximized\": false\n}";
    AppSettings s = ParseSettings(csharp);
    CHECK(s.bookmarks.size() == 1 && s.bookmarks[0].name == L"me" && s.bookmarks[0].path == L"C:\\Users\\me", "Settings: bookmarks (blank path dropped)");
    CHECK(s.folderGroups.size() == 1 && s.folderGroups[0].paths.size() == 2 && s.folderGroups[0].mode == ViewMode::LargeIcons &&
              s.folderGroups[0].modes.size() == 2 && s.folderGroups[0].modes[1] == ViewMode::MediumIcons,
          "Settings: folder groups");
    CHECK(s.left.tabs.size() == 2 && s.left.tabs[0].mode == ViewMode::MediumIcons && s.left.tabs[1].mode == ViewMode::Details &&
              s.left.selectedIndex == 1,
          "Settings: tabs (unknown mode falls back to details)");
    CHECK(s.dualPane && s.largeIconMargins.vertical == 12 && s.largeIconMargins.horizontal == 20, "Settings: dual pane and margins");
    CHECK(s.windowWidth && *s.windowWidth == 1280.5 && !s.windowHeight && s.dualWindowWidth && *s.dualWindowWidth == 1500,
          "Settings: optional window sizes");

    AppSettings round = ParseSettings(SerializeSettings(s));
    CHECK(round.bookmarks.size() == 1 && round.left.tabs.size() == 2 && round.windowWidth && *round.windowWidth == 1280.5 &&
              !round.windowHeight && round.folderGroups[0].name == L"g",
          "Settings round trip");
    CHECK(ParseSettings("{}").bookmarks.empty(), "Empty settings object");
    CHECK(!ParseSettings("{}").shortcuts.empty(), "Default shortcuts loaded");
    auto oldKeys = ParseSettings(R"({"Shortcuts":[{"Id":"parent","Key":"Alt+Up"}]})");
    auto parent = [](const AppSettings& st) -> const ShortcutItem& {
        for (const auto& item : st.shortcuts) if (item.id == "parent") return item;
        return st.shortcuts.at(0);
    };
    CHECK(parent(oldKeys).shortcut2 == L"Backspace", "Legacy settings retain default secondary shortcut");
    auto twoKeys = ParseSettings(R"({"Shortcuts":[{"Id":"parent","Key":"","Key2":"Ctrl+B"}]})");
    auto keyRound = ParseSettings(SerializeSettings(twoKeys));
    CHECK(parent(keyRound).shortcut.empty() && parent(keyRound).shortcut2 == L"Ctrl+B", "Two shortcuts and cleared slot round trip");
    CHECK(MatchShortcut(L"Backspace", 8, false, false, false), "Backspace matches");
    CHECK(MatchShortcut(L"Ctrl+VK33", 33, true, false, false), "Virtual key fallback matches");
    CHECK(!MatchShortcut(L"Ctrl+VK33", 33, false, false, false), "Virtual key modifiers enforced");

    // A corrupted settings file must not cause undefined numeric casts or oversized UI metrics.
    AppSettings invalid = ParseSettings(R"({"Left":{"SelectedIndex":1e300,"Tabs":[{"Path":"C:\\","Mode":1e300}]},
      "LargeIconMargins":{"Vertical":1e300,"Horizontal":-99999},
      "FolderGroups":[{"Name":"bad colors","Colors":[1e300,-1,4294967295]}],
      "WindowWidth":1e300,"RightPaneWidth":1e300})");
    CHECK(invalid.left.selectedIndex == 0 && invalid.left.tabs[0].mode == ViewMode::Details,
          "Invalid numeric indices and view modes use defaults");
    CHECK(invalid.largeIconMargins.vertical == 10 && invalid.largeIconMargins.horizontal == 0,
          "Invalid margins are defaulted or clamped");
    CHECK(invalid.folderGroups.size() == 1 && invalid.folderGroups[0].colors.size() == 1 &&
              invalid.folderGroups[0].colors[0] == 0xFFFFFFFFu,
          "Invalid color values are discarded");
    CHECK(!invalid.windowWidth && !invalid.rightPaneWidth,
          "Extreme window and pane sizes are ignored before pixel conversion");


    CHECK(GridPageTarget(23, 5, 16, 4, false) == 1, "Page Up preserves column in first row");
    CHECK(GridPageTarget(23, 5, 6, 4, true) == 21, "Page Down preserves column in partial last row");
    CHECK(GridPageTarget(23, 5, 9, 4, true) == 22, "Page Down falls back to final item");
    CHECK(GridPageTarget(25, 5, 6, 4, true) == 21, "Page Down preserves column in full last row");
    CHECK(GridPageTarget(23, 5, 11, 1, false) == 6, "Page Up intermediate row");
    CHECK(GridPageTarget(23, 5, 21, 4, true) == 21, "Page Down at last row stays in column");
    CHECK(GridPageTarget(3, 5, 1, 4, false) == 1, "Single row preserves column");
    CHECK(GridPageTarget(0, 5, -1, 4, true) == -1, "Empty grid");
    CHECK(DefaultShortcuts().size() == 9, "Only configurable shortcut rows remain");
    CHECK(IsFixedShortcut(L"Control+C") && IsFixedShortcut(L"VK116") && IsFixedShortcut(L"Shift+Delete"), "Reserved key aliases");
    CHECK(!IsFixedShortcut(L"Ctrl+Shift+C"), "Unreserved combination remains configurable");
    auto legacyFixed = ParseSettings(R"({"Shortcuts":[{"Id":"refresh","Key":"F9"},{"Id":"parent","Key":"Ctrl+C","Key2":"Backspace"}]})");
    CHECK(legacyFixed.shortcuts.size() == 9 && parent(legacyFixed).shortcut.empty() && parent(legacyFixed).shortcut2 == L"Backspace", "Legacy fixed overrides ignored and conflicts cleared");

    // MatchShortcut checks
    CHECK(MatchShortcut(L"F6", 0x75, false, false, false), "Match F6");
    CHECK(MatchShortcut(L"F4", 0x73, false, false, false), "Match F4");
    CHECK(MatchShortcut(L"Ctrl+F", 'F', true, false, false), "Match Ctrl+F");
    CHECK(MatchShortcut(L"Ctrl+T", 'T', true, false, false), "Match Ctrl+T");
    CHECK(!MatchShortcut(L"Ctrl+T", 'T', false, false, false), "Reject missing Ctrl");
    CHECK(MatchShortcut(L"Shift+Del", 0x2E, false, false, true), "Match Shift+Del");
    CHECK(MatchShortcut(L"Alt+Left", 0x25, false, true, false), "Match Alt+Left");
}

static void TestLru() {
    LruCache<int, int> cache(100);
    cache.Put(1, 10, 40);
    cache.Put(2, 20, 40);
    CHECK(cache.Get(1) && *cache.Get(1) == 10, "LRU hit");
    cache.Put(3, 30, 40);  // 120 > 100: 가장 오래된 2 가 밀려난다(1은 방금 사용함)
    CHECK(!cache.Contains(2) && cache.Contains(1) && cache.Contains(3), "LRU evicts least recently used");
    cache.Put(3, 31, 10);
    CHECK(*cache.Get(3) == 31 && cache.Bytes() == 50, "LRU replace updates byte count");
    cache.Put(9, 1, 1000);
    CHECK(cache.Count() == 1 && cache.Contains(9), "LRU keeps at least the newest entry");
}

static void TestQuickFind() {
    std::vector<std::wstring> names{L"ab-first", L"other", L"xxABxx", L"AB-next", L"ab-last"};
    auto nameAt = [&](int i) -> const std::wstring& { return names[i]; };
    CHECK(FindContains(L"xxABxx", L"ab"), "Find contains ignores case");
    CHECK(!FindContains(L"abc", L"ac"), "Find requires consecutive characters");
    CHECK(!FindContains(L"abc", L""), "Empty query does not match");
    CHECK(FindContains(L"보고서12", L"12"), "Find numeric query");
    CHECK(FindPrefixBelow(5, 0, L"ab", nameAt) == 3, "Find prefix below selection skips substring");
    CHECK(FindPrefixBelow(5, -1, L"ab", nameAt) == 0, "Find starts at top without selection");
    CHECK(FindPrefixBelow(5, 4, L"ab", nameAt) == -1, "Find does not wrap initial prefix search");
    CHECK(FindPrefixBelow(0, -1, L"ab", nameAt) == -1, "Find handles empty list");
    const std::vector<int> matches{0, 2, 3, 4};
    CHECK(FindNextMatch(matches, 0, false) == 2, "Tab includes substring matches");
    CHECK(FindNextMatch(matches, 4, false) == 0, "Tab wraps forward");
    CHECK(FindNextMatch(matches, 0, true) == 4, "Shift Tab wraps backward");
    CHECK(FindNextMatch(matches, 3, true) == 2, "Shift Tab moves backward");
    CHECK(FindNextMatch({}, 0, false) == -1, "Tab handles no results");
    CHECK(FindSingleKey(5, 0, L"a", nameAt) == 3, "Single key selects next prefix");
    CHECK(FindSingleKey(5, 4, L"a", nameAt) == 0, "Single key wraps");
    CHECK(FindSingleKey(5, 0, L"z", nameAt) == -1, "Single key no match");
    CHECK(FindFirstMatch(matches) == 0, "Home selects first match");
    CHECK(FindFirstMatch({}) == -1, "Home no match");
    CHECK(FindClosestBelow(matches, -1) == 0, "Find closest below without selection");
    CHECK(FindClosestBelow(matches, 0) == 0, "Find closest below matching selection");
    CHECK(FindClosestBelow(matches, 1) == 2, "Find closest below non-matching selection");
    CHECK(FindClosestBelow(matches, 3) == 3, "Find closest below matching middle");
    CHECK(FindClosestBelow(matches, 4) == 4, "Find closest below matching last");
    CHECK(FindClosestBelow(matches, 5) == 0, "Find closest below wraps around");
    CHECK(FindClosestBelow({}, 0) == -1, "Find closest below empty matches");
    const auto ranges = FindRanges(L"xxABxxab.txt", L"ab");
    CHECK(ranges.size() == 2 && ranges[0].first == 2 && ranges[0].second == 2 && ranges[1].first == 6, "Highlight only matching substrings");
    CHECK(FindRanges(L"abc", L"").empty(), "Empty query no highlights");
    CHECK(FindRanges(L"abc", L"z").empty(), "No matching ranges");

    // Wildcard matching tests
    CHECK(FindMatchesName(L"document.txt", L"*.txt"), "Wildcard matches extension");
    CHECK(FindMatchesName(L"PHOTO.PNG", L"*.png"), "Wildcard extension case insensitive");
    CHECK(!FindMatchesName(L"document.docx", L"*.txt"), "Wildcard mismatch extension");
    CHECK(FindMatchesName(L"report_2026.pdf", L"report*"), "Wildcard prefix match");
    CHECK(FindMatchesName(L"my_sample_file.cpp", L"*sample*"), "Wildcard contains match");
    CHECK(FindMatchesName(L"test.c", L"test.?"), "Wildcard single char match");
    CHECK(!FindMatchesName(L"test.cpp", L"test.?"), "Wildcard single char mismatch");
    CHECK(FindMatchesName(L"abc.txt", L"abc"), "Non-wildcard substring match");

    // Global search options test
    GlobalSearchOptions().type = SearchType::Images;
    GlobalSearchOptions().searchSubfolders = true;
    TabSearchState tabState;
    tabState.query = L"*.jpg";
    tabState.ApplyGlobalOptions();
    CHECK(tabState.type == SearchType::Images && tabState.searchSubfolders && tabState.query == L"*.jpg",
          "Global search options applied to tab state");
    GlobalSearchOptions() = {};
}

int main() {
    TestQuickFind();
    TestHistory();
    TestPaths();
    TestText();
    TestEntries();
    TestJsonAndSettings();
    TestLru();
    std::printf("\n%d checks, %d failed.\n", g_count, g_failed);
    return g_failed == 0 ? 0 : 1;
}
