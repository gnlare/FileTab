// 폴더 항목 모델, 정렬, 표시 문자열 (Models/FileEntry.cs + FolderTab.EntryComparer 포팅)
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ft {

constexpr uint32_t kAttrReadOnly = 0x1;
constexpr uint32_t kAttrHidden = 0x2;
constexpr uint32_t kAttrSystem = 0x4;
constexpr uint32_t kAttrDirectory = 0x10;
constexpr uint32_t kAttrReparsePoint = 0x400;

struct FileEntry {
    std::wstring name;
    uint64_t size = 0;       // 파일만 의미 있음
    int64_t modified = 0;    // FILETIME(1601-01-01 UTC 기준 100ns). 0 이면 알 수 없음
    uint32_t attrs = 0;
    uint32_t extId = 0;      // 소문자 확장자(점 포함)를 정수로 만든 값. 0 = 확장자 없음
    uint64_t pathHash = 0;   // 전체 경로의 해시(이미지 캐시 키용)

    bool IsDir() const { return (attrs & kAttrDirectory) != 0; }
};

enum class SortKey : int { Name = 0, Size = 1, Modified = 2 };

// 스레드 안전. 같은 확장자는 항상 같은 번호를 받는다. 빈 확장자는 0.
uint32_t InternExtension(std::wstring_view lowerCaseExtWithDot);
std::wstring ExtensionOfId(uint32_t id);

// 폴더 우선, 그 다음 선택한 열 기준, 동률이면 이름 기준. 내림차순이어도 폴더는 항상 앞에 둔다.
int  CompareEntries(const FileEntry& a, const FileEntry& b, SortKey key, bool descending);
void SortEntries(std::vector<FileEntry>& entries, SortKey key, bool descending);
// 정렬된 두 목록이 이름·크기·수정 시각·속성까지 완전히 같은지(같으면 화면을 건드릴 필요가 없다).
bool SameListing(const std::vector<FileEntry>& a, const std::vector<FileEntry>& b);

std::wstring FormatSize(int64_t bytes);   // "1.5 MB", "1,023 B"
// FILETIME 을 "yyyy-MM-dd HH:mm" 로. 변환 함수는 플랫폼별로 주입한다.
std::wstring FormatDateTimeParts(int year, int month, int day, int hour, int minute);

}  // namespace ft
