// 플랫폼 독립 문자열 유틸리티 (UTF-8 변환, 대소문자 무시 비교, 자연 정렬, 해시)
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace ft {

std::string  ToUtf8(std::wstring_view w);
std::wstring FromUtf8(std::string_view s);

wchar_t FoldChar(wchar_t c);                                    // 소문자 변환(서수 비교용)
bool    EqualsNoCase(std::wstring_view a, std::wstring_view b);
bool    StartsWithNoCase(std::wstring_view s, std::wstring_view prefix);
int     CompareNoCase(std::wstring_view a, std::wstring_view b);
std::wstring ToLower(std::wstring_view s);
std::wstring Trim(std::wstring_view s);

// 탐색기와 같은 자연 정렬(숫자 덩어리는 값으로 비교). Windows에서는 StrCmpLogicalW 사용.
int NaturalCompare(const std::wstring& a, const std::wstring& b);
int NaturalCompareFallback(const std::wstring& a, const std::wstring& b);   // 테스트 가능한 순수 구현

uint64_t HashCombine(uint64_t h, uint64_t v);
uint64_t HashNoCase(std::wstring_view s);

// 천 단위 구분 쉼표가 있는 정수 ("1,234")
std::wstring FormatThousands(int64_t v);

}  // namespace ft
