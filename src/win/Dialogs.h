// 모달 대화상자: 이름 입력, 북마크 수정, 설정, 폴더 그룹 수정/관리, 폴더 선택
#pragma once
#include "Win.h"

namespace ft {

// validate 가 오류 메시지를 돌려주면 대화상자를 닫지 않고 메시지를 보여 준다(빈 문자열 = 통과).
using NameValidator = std::function<std::wstring(const std::wstring&)>;

bool InputDialog(HWND owner, const wchar_t* title, const wchar_t* prompt, std::wstring& text, int selStart, int selEnd,
                 const wchar_t* okLabel, const NameValidator& validate);

bool BookmarkDialog(HWND owner, Bookmark& bookmark);                    // 확인하면 bookmark 를 갱신
bool SettingsDialog(HWND owner, ViewMode currentMode);                  // 확인하면 여백을 저장하고 true
bool FolderGroupEditDialog(HWND owner, FolderGroup& group);             // 확인하면 group 을 갱신(저장은 호출자)
void FolderGroupManagerDialog(HWND owner);                              // 목록·수정·삭제(저장 포함)
bool PickFolder(HWND owner, const wchar_t* title, const std::wstring& initial, std::wstring& out);

}  // namespace ft
