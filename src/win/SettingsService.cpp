#include "SettingsService.h"

#include "../core/Json.h"
#include "FsServices.h"

namespace ft {

namespace {

bool ReadWholeFile(const std::wstring& file, std::string& out) {
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart < 64 * 1024 * 1024;
    if (ok) {
        out.resize(static_cast<size_t>(size.QuadPart));
        DWORD got = 0;
        ok = size.QuadPart == 0 || (ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size());
    }
    CloseHandle(h);
    return ok;
}

bool WriteWholeFile(const std::wstring& file, const std::string& data) {
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
    ok = FlushFileBuffers(h) && ok;
    CloseHandle(h);
    return ok;
}

}  // namespace

SettingsService& SettingsService::Get() {
    static SettingsService* instance = new SettingsService();
    return *instance;
}

SettingsService::SettingsService() {
    std::wstring dir;
    wchar_t env[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(L"FILETAB_SETTINGS_DIR", env, MAX_PATH * 2);
    if (n > 0 && n < MAX_PATH * 2) dir = env;
    else dir = path::Combine(KnownFolder(FOLDERID_LocalAppData), L"FileTab");
    file_ = path::Combine(dir, L"settings.json");

    bool existed = PathExists(file_);
    if (existed) {
        std::string text;
        if (!ReadWholeFile(file_, text)) {
            loadWarning = L"설정을 읽지 못해 기본 설정으로 시작했습니다: " + FormatWin32Error(GetLastError());
        } else {
            try {
                state = ParseSettings(text);
            } catch (const std::exception& ex) {
                state = AppSettings();
                loadWarning = L"설정을 읽지 못해 기본 설정으로 시작했습니다: " + FromUtf8(ex.what());
            }
        }
    }
    if (!existed || !loadWarning.empty()) {
        for (const std::wstring& p : {KnownFolder(FOLDERID_Profile), KnownFolder(FOLDERID_Documents)}) {
            if (p.empty() || !IsDirectoryPath(p)) continue;
            std::wstring trimmed = p;
            while (!trimmed.empty() && trimmed.back() == L'\\') trimmed.pop_back();
            state.bookmarks.push_back({path::FileName(trimmed), p});
        }
    }
}

bool SettingsService::AddBookmark(const std::wstring& pathIn, std::wstring* error) {
    std::wstring p, why;
    if (!path::TryNormalize(pathIn, L"", p, &why)) {
        if (error) *error = why;
        return false;
    }
    if (!IsDirectoryPath(p)) {
        if (error) *error = L"폴더를 찾을 수 없습니다: " + p;
        return false;
    }
    for (const auto& b : state.bookmarks)
        if (EqualsNoCase(b.path, p)) return false;
    std::wstring name = path::Label(p);
    state.bookmarks.push_back({name, p});
    return true;
}

bool SettingsService::Save(std::wstring* error) {
    std::wstring dir = path::Parent(file_);
    if (!dir.empty()) SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    std::wstring temp = file_ + L".tmp";
    std::string json = SerializeSettings(state);
    if (!WriteWholeFile(temp, json) || !MoveFileExW(temp.c_str(), file_.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        if (error) *error = FormatWin32Error(GetLastError());
        return false;
    }
    return true;
}

}  // namespace ft
