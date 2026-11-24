#include "FsServices.h"
#include "core/QuickFind.h"

#include "Workers.h"

namespace ft {

// ======================= 폴더 읽기 =======================

bool PathExists(const std::wstring& path) { return GetFileAttributesW(LongPath(path).c_str()) != INVALID_FILE_ATTRIBUTES; }

bool IsDirectoryPath(const std::wstring& path) {
    DWORD a = GetFileAttributesW(LongPath(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool ReadDirectory(const std::wstring& path, std::vector<FileEntry>& out, std::wstring& error,
                   const std::atomic<bool>& cancel) {
    std::wstring pattern = LongPath(path::Combine(path, L"*"));
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND) return true;  // 빈 폴더(드문 경우)
        error = (code == ERROR_PATH_NOT_FOUND || code == ERROR_BAD_NETPATH || code == ERROR_INVALID_NAME)
                    ? L"폴더를 찾을 수 없습니다: " + path
                    : FormatWin32Error(code);
        return false;
    }
    const uint64_t dirHash = HashNoCase(path::Combine(path, L""));
    out.clear();
    int counter = 0;
    do {
        const wchar_t* n = fd.cFileName;
        if (n[0] == L'.' && (n[1] == L'\0' || (n[1] == L'.' && n[2] == L'\0'))) continue;
        if ((++counter & 255) == 0 && cancel.load(std::memory_order_relaxed)) {
            FindClose(h);
            error = L"취소됨";
            return false;
        }
        FileEntry e;
        e.name = n;
        e.attrs = fd.dwFileAttributes;
        if (!e.IsDir()) e.size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        e.modified = static_cast<int64_t>((static_cast<uint64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32) |
                                          fd.ftLastWriteTime.dwLowDateTime);
        if (!e.IsDir()) e.extId = InternExtension(ToLower(path::Extension(e.name)));
        e.pathHash = HashCombine(dirHash, HashNoCase(e.name));
        out.push_back(std::move(e));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}

namespace {
bool IsImageExtension(std::wstring_view ext) {
    return EqualsNoCase(ext, L".png") || EqualsNoCase(ext, L".jpg") || EqualsNoCase(ext, L".jpeg") ||
           EqualsNoCase(ext, L".bmp") || EqualsNoCase(ext, L".gif") || EqualsNoCase(ext, L".webp") ||
           EqualsNoCase(ext, L".ico") || EqualsNoCase(ext, L".tiff") || EqualsNoCase(ext, L".tif") ||
           EqualsNoCase(ext, L".svg") || EqualsNoCase(ext, L".jfif");
}

bool IsVideoExtension(std::wstring_view ext) {
    return EqualsNoCase(ext, L".mp4") || EqualsNoCase(ext, L".mkv") || EqualsNoCase(ext, L".avi") ||
           EqualsNoCase(ext, L".wmv") || EqualsNoCase(ext, L".mov") || EqualsNoCase(ext, L".webm") ||
           EqualsNoCase(ext, L".flv") || EqualsNoCase(ext, L".m4v") || EqualsNoCase(ext, L".ts") ||
           EqualsNoCase(ext, L".mpg") || EqualsNoCase(ext, L".mpeg") || EqualsNoCase(ext, L".3gp");
}

bool IsTextFileExtension(std::wstring_view ext) {
    static const wchar_t* const textExts[] = {
        L".txt", L".log", L".md", L".json", L".xml", L".html", L".htm", L".css",
        L".js", L".ts", L".jsx", L".tsx", L".cpp", L".c", L".h", L".hpp", L".cs",
        L".py", L".java", L".ini", L".cfg", L".bat", L".cmd", L".ps1", L".yaml",
        L".yml", L".csv", L".tsv", L".sql", L".sh", L".rc", L".cmake", L".config",
        L".properties", L".rst", L".tex", L".rs", L".go", L".rb", L".php", L".lua"
    };
    for (const auto* t : textExts) {
        if (EqualsNoCase(ext, t)) return true;
    }
    return false;
}

bool FileContainsText(const std::wstring& fullPath, const std::wstring& query) {
    if (query.empty()) return true;
    HANDLE h = CreateFileW(LongPath(fullPath).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart == 0 || sz.QuadPart > 20 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }

    DWORD bytesToRead = static_cast<DWORD>(sz.QuadPart);
    std::string buf(bytesToRead, '\0');
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(h, buf.data(), bytesToRead, &bytesRead, nullptr);
    CloseHandle(h);
    if (!ok || bytesRead == 0) return false;
    buf.resize(bytesRead);

    std::string queryUtf8 = ToUtf8(query);
    auto toLowerA = [](std::string s) {
        for (char& c : s) if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
        return s;
    };
    std::string lquery = toLowerA(queryUtf8);
    std::string lbuf = toLowerA(buf);
    if (lbuf.find(lquery) != std::string::npos) return true;

    if (bytesRead >= sizeof(wchar_t)) {
        size_t wlen = bytesRead / sizeof(wchar_t);
        const wchar_t* wbuf = reinterpret_cast<const wchar_t*>(buf.data());
        std::wstring ws(wbuf, wlen);
        if (FindContains(ws, query)) return true;
    }
    return false;
}
}  // namespace

bool SearchFiles(const std::wstring& rootPath, const TabSearchState& search, std::vector<FileEntry>& out,
                 std::wstring& error, const std::atomic<bool>& cancel) {
    out.clear();
    std::vector<std::wstring> dirs;
    dirs.push_back(rootPath);
    int counter = 0;

    while (!dirs.empty()) {
        if (cancel.load(std::memory_order_relaxed)) {
            error = L"취소됨";
            return false;
        }
        std::wstring curDir = std::move(dirs.back());
        dirs.pop_back();

        std::wstring pattern = LongPath(path::Combine(curDir, L"*"));
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                    FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) continue;

        do {
            const wchar_t* n = fd.cFileName;
            if (n[0] == L'.' && (n[1] == L'\0' || (n[1] == L'.' && n[2] == L'\0'))) continue;
            if ((++counter & 255) == 0 && cancel.load(std::memory_order_relaxed)) {
                FindClose(h);
                error = L"취소됨";
                return false;
            }

            bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            std::wstring fullPath = path::Combine(curDir, n);

            if (isDir && search.searchSubfolders) {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                    dirs.push_back(fullPath);
                }
            }

            // 1. 이름 검색 (와일드카드 * 또는 ? 지원, 없으면 부분 문자열 포함 여부 확인)
            if (!search.query.empty()) {
                bool match = false;
                if (search.query.find_first_of(L"*?") != std::wstring::npos) {
                    match = (PathMatchSpecW(n, search.query.c_str()) != FALSE);
                    if (!match && search.searchSubfolders && search.query.find_first_of(L"\\/") != std::wstring::npos) {
                        std::wstring relDir = curDir.substr(rootPath.size());
                        if (!relDir.empty() && (relDir[0] == L'\\' || relDir[0] == L'/')) relDir = relDir.substr(1);
                        std::wstring relPath = path::Combine(relDir, n);
                        match = (PathMatchSpecW(relPath.c_str(), search.query.c_str()) != FALSE);
                    }
                } else {
                    match = FindContains(n, search.query);
                }
                if (!match) continue;
            }

            // 2. 유형 필터
            if (search.type == SearchType::FilesOnly && isDir) continue;
            if (search.type == SearchType::Images) {
                if (isDir || !IsImageExtension(path::Extension(n))) continue;
            }
            if (search.type == SearchType::Videos) {
                if (isDir || !IsVideoExtension(path::Extension(n))) continue;
            }

            // 3. 포함 문자 필터
            if (!search.contentQuery.empty()) {
                if (isDir) continue;
                if (!IsTextFileExtension(path::Extension(n))) continue;
                if (!FileContainsText(fullPath, search.contentQuery)) continue;
            }

            FileEntry e;
            if (search.searchSubfolders && !EqualsNoCase(curDir, rootPath)) {
                std::wstring relDir = curDir.substr(rootPath.size());
                if (!relDir.empty() && (relDir[0] == L'\\' || relDir[0] == L'/')) relDir = relDir.substr(1);
                e.name = path::Combine(relDir, n);
            } else {
                e.name = n;
            }
            e.attrs = fd.dwFileAttributes;
            if (!e.IsDir()) e.size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            e.modified = static_cast<int64_t>((static_cast<uint64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32) |
                                              fd.ftLastWriteTime.dwLowDateTime);
            if (!e.IsDir()) e.extId = InternExtension(ToLower(path::Extension(n)));
            e.pathHash = HashNoCase(fullPath);
            out.push_back(std::move(e));

            if (out.size() >= 10000) {
                FindClose(h);
                return true;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return true;
}

std::wstring ResolveDirectoryLink(const std::wstring& path) {
    DWORD a = GetFileAttributesW(LongPath(path).c_str());
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_REPARSE_POINT)) return path;
    HANDLE h = CreateFileW(LongPath(path).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return path;
    wchar_t buf[2048];
    DWORD n = GetFinalPathNameByHandleW(h, buf, 2048, FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (n == 0 || n >= 2048) return path;
    std::wstring r(buf, n);
    if (r.compare(0, 8, L"\\\\?\\UNC\\") == 0) r = L"\\\\" + r.substr(8);
    else if (r.compare(0, 4, L"\\\\?\\") == 0) r = r.substr(4);
    return IsDirectoryPath(r) ? r : path;
}

std::wstring ResolveShortcutTarget(const std::wstring& lnkPath) {
    if (lnkPath.size() < 4 || !EqualsNoCase(lnkPath.substr(lnkPath.size() - 4), L".lnk")) return std::wstring();
    std::wstring target;
    IShellLinkW* link = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
            if (SUCCEEDED(file->Load(lnkPath.c_str(), STGM_READ))) {
                link->Resolve(nullptr, SLR_NO_UI | SLR_NOTRACK);
                wchar_t buf[MAX_PATH * 4];
                buf[0] = L'\0';
                if (SUCCEEDED(link->GetPath(buf, MAX_PATH * 4, nullptr, 0))) target = buf;
            }
            file->Release();
        }
        link->Release();
    }
    return target;
}

// ======================= 변경 감시 =======================

void DirWatcher::Start(const std::wstring& dir, HWND notify, UINT msg, uintptr_t generation, LPARAM sender) {
    Stop();
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE stop = stop_;
    thread_ = std::thread(&DirWatcher::Run, dir, notify, msg, generation, sender, stop);
}

void DirWatcher::Stop() {
    if (!thread_.joinable()) return;
    SetEvent(stop_);
    thread_.join();
    CloseHandle(stop_);
    stop_ = nullptr;
}

void DirWatcher::Run(std::wstring dir, HWND notify, UINT msg, uintptr_t generation, LPARAM sender, HANDLE stopEvent) {
    HANDLE h = CreateFileW(LongPath(dir).c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;   // 감시할 수 없는 위치: F5 로 갱신
    HANDLE ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    alignas(DWORD) BYTE buffer[16 * 1024];
    for (;;) {
        OVERLAPPED ov{};
        ov.hEvent = ioEvent;
        ResetEvent(ioEvent);
        DWORD returned = 0;
        if (!ReadDirectoryChangesW(h, buffer, sizeof buffer, FALSE,
                                   FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE |
                                       FILE_NOTIFY_CHANGE_LAST_WRITE,
                                   &returned, &ov, nullptr))
            break;
        HANDLE handles[2] = {ioEvent, stopEvent};
        DWORD w = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (w != WAIT_OBJECT_0) {
            CancelIoEx(h, &ov);
            DWORD n = 0;
            GetOverlappedResult(h, &ov, &n, TRUE);
            break;
        }
        PostMessageW(notify, msg, static_cast<WPARAM>(generation), sender);
        // 큰 복사 중 이벤트 폭주를 200ms 단위로 묶는다(그동안의 변경은 커널 버퍼에 쌓인다).
        if (WaitForSingleObject(stopEvent, 200) == WAIT_OBJECT_0) break;
    }
    CloseHandle(ioEvent);
    CloseHandle(h);
}

// ======================= Shell 파일 작업 =======================

namespace {

StaWorker& OpsWorker() {
    static StaWorker* worker = new StaWorker();   // 의도적으로 해제하지 않음
    return *worker;
}

void PostResult(bool ok, const std::wstring& error) {
    auto* r = new OpResult{ok, error};
    if (!g_mainWnd || !PostMessageW(g_mainWnd, WM_FT_OPDONE, 0, reinterpret_cast<LPARAM>(r))) delete r;
}

std::wstring HrMessage(HRESULT hr) { return FormatWin32Error(HRESULT_CODE(hr)); }

std::wstring UniqueCopyName(const std::wstring& source, const std::wstring& destination, std::vector<std::wstring>& reserved) {
    std::wstring name = path::FileName(source);
    bool isDir = IsDirectoryPath(source);
    std::wstring ext = isDir ? std::wstring() : path::Extension(name);
    std::wstring stem = ext.empty() ? name : name.substr(0, name.size() - ext.size());
    for (int index = 1;; ++index) {
        std::wstring candidate = stem + L" - 복사본" + (index == 1 ? L"" : L" (" + std::to_wstring(index) + L")") + ext;
        bool taken = PathExists(path::Combine(destination, candidate));
        for (const auto& r : reserved)
            if (EqualsNoCase(r, candidate)) taken = true;
        if (!taken) {
            reserved.push_back(candidate);
            return candidate;
        }
    }
}

enum class OpKind { Copy, Move, Delete, Rename };

// STA 스레드에서 실행. 실패 메시지를 돌려주고, 성공이면 빈 문자열. aborted 는 사용자가 취소했는지.
std::wstring PerformOperation(OpKind kind, const std::vector<std::wstring>& paths, const std::wstring& destination,
                              const std::wstring& newName, HWND owner, bool recycle, bool& aborted) {
    aborted = false;
    for (const auto& p : paths) {
        if (!PathExists(p)) return L"원본 항목이 없습니다: " + p;
        if (kind == OpKind::Copy || kind == OpKind::Move) {
            std::wstring err = path::ValidateCopy(p, destination, IsDirectoryPath(p));
            if (!err.empty()) return err;
        }
    }
    if ((kind == OpKind::Copy || kind == OpKind::Move) && !IsDirectoryPath(destination)) return L"대상 폴더를 찾을 수 없습니다: " + destination;

    IFileOperation* op = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op));
    if (FAILED(hr) || !op) return L"Windows 파일 작업을 시작하지 못했습니다: " + HrMessage(hr);

    DWORD flags = 0;
    if (kind != OpKind::Delete || recycle) flags |= 0x0040 | 0x4000 | 0x20000000;  // ALLOWUNDO | WANTNUKEWARNING | ADDUNDORECORD
    if (kind == OpKind::Delete && recycle) flags |= 0x00080000;                    // RECYCLEONDELETE
    if (kind == OpKind::Delete && !recycle) flags |= 0x0010;                       // NOCONFIRMATION(영구 삭제는 앱에서 이미 확인함)
    op->SetOperationFlags(flags);
    op->SetOwnerWindow(owner);

    std::vector<IShellItem*> items;
    IShellItem* target = nullptr;
    std::wstring failure;
    if (kind == OpKind::Copy || kind == OpKind::Move) {
        hr = SHCreateItemFromParsingName(destination.c_str(), nullptr, IID_PPV_ARGS(&target));
        if (FAILED(hr)) failure = L"대상 폴더를 열 수 없습니다: " + HrMessage(hr);
    }
    std::vector<std::wstring> reserved;
    for (size_t i = 0; i < paths.size() && failure.empty(); ++i) {
        IShellItem* item = nullptr;
        hr = SHCreateItemFromParsingName(paths[i].c_str(), nullptr, IID_PPV_ARGS(&item));
        if (FAILED(hr) || !item) {
            failure = L"항목을 열 수 없습니다: " + paths[i];
            break;
        }
        items.push_back(item);
        if (kind == OpKind::Delete) {
            hr = op->DeleteItem(item, nullptr);
        } else if (kind == OpKind::Rename) {
            hr = op->RenameItem(item, newName.c_str(), nullptr);
        } else if (kind == OpKind::Move) {
            hr = op->MoveItem(item, target, nullptr, nullptr);
        } else {
            std::wstring parent = path::Parent(paths[i]);
            bool sameFolder = !parent.empty() && EqualsNoCase(parent, destination);
            std::wstring copyName;
            if (sameFolder) copyName = UniqueCopyName(paths[i], destination, reserved);
            hr = op->CopyItem(item, target, sameFolder ? copyName.c_str() : nullptr, nullptr);
        }
        if (FAILED(hr)) failure = HrMessage(hr);
    }
    if (failure.empty()) {
        hr = op->PerformOperations();
        if (FAILED(hr)) failure = HrMessage(hr);
        BOOL a = FALSE;
        if (SUCCEEDED(op->GetAnyOperationsAborted(&a))) aborted = a != FALSE;
    }
    op->Release();   // 큐에 넣은 항목보다 먼저 같은 STA 에서 해제
    for (IShellItem* item : items) item->Release();
    if (target) target->Release();
    return failure;
}

void Dispatch(OpKind kind, std::vector<std::wstring> paths, std::wstring destination, std::wstring newName, HWND owner,
              bool recycle) {
    OpsWorker().Post([=]() {
        bool aborted = false;
        std::wstring err = PerformOperation(kind, paths, destination, newName, owner, recycle, aborted);
        PostResult(err.empty(), err);
    });
}

}  // namespace

void RunCopy(std::vector<std::wstring> paths, std::wstring destination, HWND owner) {
    Dispatch(OpKind::Copy, std::move(paths), std::move(destination), L"", owner, true);
}
void RunMove(std::vector<std::wstring> paths, std::wstring destination, HWND owner) {
    Dispatch(OpKind::Move, std::move(paths), std::move(destination), L"", owner, true);
}
void RunDelete(std::vector<std::wstring> paths, HWND owner, bool recycle) {
    Dispatch(OpKind::Delete, std::move(paths), L"", L"", owner, recycle);
}
void RunRename(std::wstring path, std::wstring newName, HWND owner) {
    Dispatch(OpKind::Rename, {std::move(path)}, L"", std::move(newName), owner, true);
}

// ======================= 클립보드 =======================

namespace {
bool OpenClipboardRetry(HWND owner) {
    for (int i = 0; i < 10; ++i) {
        if (OpenClipboard(owner)) return true;
        Sleep(20);
    }
    return false;
}
}  // namespace

bool CopyFilesToClipboard(HWND owner, const std::vector<std::wstring>& paths) {
    if (paths.empty()) return false;
    size_t chars = 1;
    for (const auto& p : paths) chars += p.size() + 1;
    HGLOBAL files = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + chars * sizeof(wchar_t));
    HGLOBAL effect = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (!files || !effect) {
        if (files) GlobalFree(files);
        if (effect) GlobalFree(effect);
        return false;
    }
    auto* df = static_cast<DROPFILES*>(GlobalLock(files));
    if (!df) {
        GlobalFree(files);
        GlobalFree(effect);
        return false;
    }
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    auto* dst = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(df) + sizeof(DROPFILES));
    for (const auto& p : paths) {
        for (wchar_t c : p) *dst++ = c;
        *dst++ = L'\0';
    }
    *dst = L'\0';
    GlobalUnlock(files);
    auto* dropEffect = static_cast<DWORD*>(GlobalLock(effect));
    if (!dropEffect) {
        GlobalFree(files);
        GlobalFree(effect);
        return false;
    }
    *dropEffect = DROPEFFECT_COPY;
    GlobalUnlock(effect);

    if (!OpenClipboardRetry(owner)) {
        GlobalFree(files);
        GlobalFree(effect);
        return false;
    }
    EmptyClipboard();
    static const UINT kPreferred = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    bool ok = SetClipboardData(CF_HDROP, files) != nullptr;
    if (!ok) GlobalFree(files);
    if (SetClipboardData(kPreferred, effect) == nullptr) GlobalFree(effect);
    CloseClipboard();
    return ok;
}

bool ClipboardHasFiles() { return IsClipboardFormatAvailable(CF_HDROP) != FALSE; }

std::vector<std::wstring> ReadClipboardFiles(HWND owner) {
    std::vector<std::wstring> result;
    if (!ClipboardHasFiles() || !OpenClipboardRetry(owner)) return result;
    if (HANDLE data = GetClipboardData(CF_HDROP)) {
        HDROP drop = static_cast<HDROP>(data);
        UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            UINT len = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring s(len + 1, L'\0');
            DragQueryFileW(drop, i, s.data(), len + 1);
            s.resize(len);
            bool dup = false;
            for (const auto& e : result)
                if (EqualsNoCase(e, s)) dup = true;
            if (!dup) result.push_back(std::move(s));
        }
    }
    CloseClipboard();
    return result;
}

// ======================= 드래그 =======================

namespace {
class DropSource final : public IDropSource {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --refs_;
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP QueryContinueDrag(BOOL escape, DWORD keyState) override {
        if (escape) return DRAGDROP_S_CANCEL;
        if (!(keyState & (MK_LBUTTON | MK_RBUTTON))) return DRAGDROP_S_DROP;
        return S_OK;
    }
    STDMETHODIMP GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }

private:
    ULONG refs_ = 1;
};
}  // namespace

void StartFileDrag(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return;
    std::vector<PIDLIST_ABSOLUTE> pidls;
    for (const auto& p : paths) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(p.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) pidls.push_back(pidl);
    }
    if (pidls.empty()) return;
    std::vector<PCUITEMID_CHILD> apidl(pidls.begin(), pidls.end());
    IDataObject* data = nullptr;
    HRESULT hr = SHCreateDataObject(nullptr, static_cast<UINT>(apidl.size()),
                                    apidl.data(), nullptr,
                                    IID_PPV_ARGS(&data));
    if (SUCCEEDED(hr) && data) {
        // CF_HDROP 설정: 외부 윈도우 앱과의 호환성을 위해 추가
        size_t chars = 1;
        for (const auto& p : paths) chars += p.size() + 1;
        HGLOBAL files = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + chars * sizeof(wchar_t));
        if (files) {
            auto* df = static_cast<DROPFILES*>(GlobalLock(files));
            if (df) {
                df->pFiles = sizeof(DROPFILES);
                df->fWide = TRUE;
                auto* dst = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(df) + sizeof(DROPFILES));
                for (const auto& p : paths) {
                    for (wchar_t c : p) *dst++ = c;
                    *dst++ = L'\0';
                }
                *dst = L'\0';
                GlobalUnlock(files);

                FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
                STGMEDIUM med{};
                med.tymed = TYMED_HGLOBAL;
                med.hGlobal = files;
                if (FAILED(data->SetData(&fmt, &med, TRUE))) {
                    GlobalFree(files);
                }
            } else {
                GlobalFree(files);
            }
        }

        // CFSTR_PREFERREDDROPEFFECT 설정
        UINT cfPreferred = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
        HGLOBAL hEffect = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
        if (hEffect) {
            auto* pEffect = static_cast<DWORD*>(GlobalLock(hEffect));
            if (pEffect) {
                *pEffect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
                GlobalUnlock(hEffect);
                FORMATETC fmtEff{static_cast<CLIPFORMAT>(cfPreferred), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
                STGMEDIUM medEff{};
                medEff.tymed = TYMED_HGLOBAL;
                medEff.hGlobal = hEffect;
                if (FAILED(data->SetData(&fmtEff, &medEff, TRUE))) {
                    GlobalFree(hEffect);
                }
            } else {
                GlobalFree(hEffect);
            }
        }

        auto* source = new DropSource();
        DWORD effect = DROPEFFECT_NONE;
        DoDragDrop(data, source, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
        source->Release();
        data->Release();
    }
    for (auto pidl : pidls) CoTaskMemFree(pidl);
}

// ======================= 열기 / 폴더 만들기 =======================

bool OpenWithShell(HWND owner, const std::wstring& path, std::wstring* error) {
    std::wstring dir = path::Parent(path);
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof info;
    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.hwnd = owner;
    info.lpFile = path.c_str();
    info.lpDirectory = dir.empty() ? nullptr : dir.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info)) return true;
    if (error) *error = FormatWin32Error(GetLastError());
    return false;
}

bool CreateDirectories(HWND owner, const std::wstring& path, std::wstring* error) {
    int rc = SHCreateDirectoryExW(owner, path.c_str(), nullptr);
    if (rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS) return true;
    if (rc == ERROR_CANCELLED) return false;
    if (error) *error = FormatWin32Error(static_cast<DWORD>(rc));
    return false;
}

}  // namespace ft
