// 설정 데이터 모델과 JSON 변환. C# 버전의 settings.json 과 같은 속성 이름을 사용하므로 그대로 읽을 수 있다.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ft {

enum class ViewMode : int { Details = 0, LargeIcons = 1, MediumIcons = 2 };

struct Bookmark {
    std::wstring name;
    std::wstring path;
};

struct FolderGroup {
    std::wstring name;
    std::vector<std::wstring> paths;
    ViewMode mode = ViewMode::Details;
    std::vector<ViewMode> modes;
    std::vector<uint32_t> colors;
};

struct TabState {
    std::wstring path;
    ViewMode mode = ViewMode::Details;
};

struct PaneState {
    std::vector<TabState> tabs;
    int selectedIndex = 0;
};

struct IconMargins {
    int vertical = 10;
    int horizontal = 16;
};

struct ShortcutItem {
    std::string id;
    std::wstring name;
    std::wstring shortcut;
    std::wstring shortcut2{};
};

std::vector<ShortcutItem> DefaultShortcuts();
bool IsFixedShortcutKey(unsigned int key, bool ctrl, bool alt, bool shift);
bool IsFixedShortcut(const std::wstring& shortcut);
bool MatchShortcut(const std::wstring& shortcutStr, unsigned int key, bool ctrl, bool alt, bool shift);

struct AppSettings {
    std::vector<Bookmark> bookmarks;
    std::vector<FolderGroup> folderGroups;
    PaneState left;
    PaneState right;
    bool dualPane = false;
    IconMargins largeIconMargins;
    IconMargins mediumIconMargins;
    std::vector<ShortcutItem> shortcuts = DefaultShortcuts();
    std::optional<double> windowWidth;       // DIP 단위(C# 버전과 동일)
    std::optional<double> windowHeight;
    std::optional<double> dualWindowWidth;
    std::optional<double> leftPaneWidth;
    std::optional<double> rightPaneWidth;
    bool isMaximized = false;
    bool darkMode = false;
};

ViewMode ClampViewMode(int value);

// 문법 오류면 JsonError. 값 형식이 틀린 속성은 기본값으로 둔다.
AppSettings ParseSettings(std::string_view json);
std::string SerializeSettings(const AppSettings& settings);

}  // namespace ft
