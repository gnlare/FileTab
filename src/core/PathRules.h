// 경로 정규화·검사 규칙 (FileTab.Core/PathRules.cs 의 C++ 포팅). 플랫폼 독립 순수 문자열 처리.
#pragma once
#include <string>
#include <vector>

namespace ft::path {

constexpr wchar_t kSep = L'\\';

// 환경 변수 확장 훅. Windows 앱 시작 시 ExpandEnvironmentStringsW 기반 함수를 등록한다.
using EnvExpander = std::wstring (*)(const std::wstring&);
void SetEnvExpander(EnvExpander fn);

// 입력(따옴표·환경 변수·상대 경로·드라이브만 입력 포함)을 절대 경로로 정규화한다.
// 실패하면 false 와 사용자에게 보여줄 메시지를 돌려준다.
bool TryNormalize(const std::wstring& input, const std::wstring& base, std::wstring& out, std::wstring* error = nullptr);

std::wstring RootOf(const std::wstring& normalized);        // "C:\" 또는 "\\server\share" (없으면 빈 문자열)
bool         IsRoot(const std::wstring& normalized);
std::wstring Parent(const std::wstring& normalized);        // 루트면 빈 문자열
std::wstring Combine(const std::wstring& dir, const std::wstring& name);
std::wstring FileName(const std::wstring& p);               // 마지막 구성 요소
std::wstring Label(const std::wstring& p);                  // 탭 제목용: 마지막 폴더명, 없으면 전체 경로
std::wstring Extension(const std::wstring& name);           // 마지막 '.' 부터. 없으면 빈 문자열
std::wstring StemOf(const std::wstring& name);              // 확장자를 뺀 이름

bool IsSameOrDescendant(const std::wstring& candidate, const std::wstring& ancestor);

// 오류 메시지를 돌려주고, 문제가 없으면 빈 문자열.
std::wstring ValidateName(const std::wstring& name);
std::wstring ValidateCopy(const std::wstring& source, const std::wstring& destinationDirectory, bool isDirectory);

struct Crumb {
    std::wstring label;
    std::wstring path;
};
std::vector<Crumb> Breadcrumbs(const std::wstring& normalizedPath);

}  // namespace ft::path
