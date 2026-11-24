// 파일 시스템 서비스: 폴더 읽기, 변경 감시, Shell 파일 작업, 클립보드, 바로 가기, 드래그
#pragma once
#include "Win.h"

#include <thread>

namespace ft {

// ---------------- 폴더 읽기 ----------------
// FindFirstFileEx(기본 정보만, 큰 버퍼)로 한 번에 크기·시각·속성을 읽는다. 항목마다 따로 파일을 열지 않는다.
bool ReadDirectory(const std::wstring& path, std::vector<FileEntry>& out, std::wstring& error,
                   const std::atomic<bool>& cancel);
bool SearchFiles(const std::wstring& rootPath, const TabSearchState& search, std::vector<FileEntry>& out,
                 std::wstring& error, const std::atomic<bool>& cancel);

bool PathExists(const std::wstring& path);
bool IsDirectoryPath(const std::wstring& path);

// 대상 위치가 바로 가기/심볼릭 링크 폴더면 실제 위치를, 아니면 입력을 그대로 돌려준다.
std::wstring ResolveDirectoryLink(const std::wstring& path);
// .lnk 대상 경로(없으면 빈 문자열). UI 스레드(STA)에서 호출한다.
std::wstring ResolveShortcutTarget(const std::wstring& lnkPath);

// ---------------- 변경 감시 ----------------
class DirWatcher {
public:
    ~DirWatcher() { Stop(); }
    // 변경이 생기면 notify 창에 msg(wParam=generation, lParam=sender)를 보낸다(연속 변경은 200ms 단위로 묶임).
    void Start(const std::wstring& dir, HWND notify, UINT msg, uintptr_t generation, LPARAM sender);
    void Stop();

private:
    static void Run(std::wstring dir, HWND notify, UINT msg, uintptr_t generation, LPARAM sender, HANDLE stopEvent);
    std::thread thread_;
    HANDLE stop_ = nullptr;
};

// ---------------- Shell 파일 작업 (IFileOperation) ----------------
// 전용 STA 스레드에서 실행하고, 끝나면 메인 창에 WM_FT_OPDONE(OpResult*)을 보낸다.
void RunCopy(std::vector<std::wstring> paths, std::wstring destination, HWND owner);
void RunMove(std::vector<std::wstring> paths, std::wstring destination, HWND owner);
void RunDelete(std::vector<std::wstring> paths, HWND owner, bool recycle);
void RunRename(std::wstring path, std::wstring newName, HWND owner);

// ---------------- 클립보드 ----------------
bool CopyFilesToClipboard(HWND owner, const std::vector<std::wstring>& paths);   // 복사(Preferred DropEffect=COPY)
bool ClipboardHasFiles();
std::vector<std::wstring> ReadClipboardFiles(HWND owner);

// ---------------- 드래그 시작 ----------------
// 선택한 파일을 OLE 드래그로 내보낸다(북마크 바나 탐색기에 놓을 수 있다). 동기 함수(드롭이 끝나면 반환).
void StartFileDrag(const std::vector<std::wstring>& paths);

// ---------------- 열기 ----------------
bool OpenWithShell(HWND owner, const std::wstring& path, std::wstring* error);
bool CreateDirectories(HWND owner, const std::wstring& path, std::wstring* error);

}  // namespace ft
