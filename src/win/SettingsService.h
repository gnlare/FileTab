// 설정 파일(%LOCALAPPDATA%\FileTab\settings.json) 읽기/쓰기. 공용 북마크와 폴더 그룹을 소유한다.
#pragma once
#include "Win.h"

namespace ft {

class SettingsService {
public:
    static SettingsService& Get();

    AppSettings state;
    std::wstring loadWarning;

    IconMargins& Margins(ViewMode mode) {
        return mode == ViewMode::MediumIcons ? state.mediumIconMargins : state.largeIconMargins;
    }
    // 북마크 추가. 이미 있으면 false(error 는 비어 있음), 경로 오류면 false + error.
    bool AddBookmark(const std::wstring& path, std::wstring* error);
    bool Save(std::wstring* error);   // 임시 파일에 쓴 뒤 교체한다

private:
    SettingsService();
    std::wstring file_;
};

}  // namespace ft
