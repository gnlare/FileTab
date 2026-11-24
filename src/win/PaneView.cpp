// PaneView: 창 생성, 탭 모델, 동작(열기·새 폴더·북마크·폴더 그룹), 메뉴, 드롭 대상
#include "PaneView.h"

#include <algorithm>

#include "Dialogs.h"
#include "SettingsService.h"

namespace ft {

namespace {
const wchar_t* kPaneClass = L"FileTabPane";
constexpr UINT_PTR kRefreshTimer = 1;

enum Cmd : UINT {
    kCmdOpen = 1, kCmdNewFolder, kCmdCopy, kCmdPaste, kCmdRename, kCmdDelete, kCmdBookmarkSel, kCmdBookmarkCur,
    kCmdViewDetails = 20, kCmdViewLarge, kCmdViewMedium, kCmdRefresh, kCmdCloseTab = 40, kCmdBmEdit = 50, kCmdBmDelete,
    kCmdGroupEdit = 60, kCmdGroupDelete
};
}  // namespace

// ======================= 북마크 드롭 대상 (탐색기에서 폴더를 끌어다 놓기) =======================

class PaneDropTarget final : public IDropTarget {
public:
    explicit PaneDropTarget(PaneView* pane) : pane_(pane) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
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
    STDMETHODIMP DragEnter(IDataObject* data, DWORD keyState, POINTL, DWORD* effect) override {
        FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        hasFiles_ = data && SUCCEEDED(data->QueryGetData(&fmt));
        *effect = Effect(keyState);
        return S_OK;
    }
    STDMETHODIMP DragOver(DWORD keyState, POINTL, DWORD* effect) override {
        *effect = Effect(keyState);
        return S_OK;
    }
    STDMETHODIMP DragLeave() override {
        hasFiles_ = false;
        return S_OK;
    }
    STDMETHODIMP Drop(IDataObject* data, DWORD keyState, POINTL pt, DWORD* effect) override {
        *effect = DROPEFFECT_NONE;
        if (!data || !hasFiles_) return S_OK;
        FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        if (SUCCEEDED(data->GetData(&fmt, &medium))) {
            std::vector<std::wstring> paths;
            HDROP drop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
            if (drop) {
                UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                for (UINT i = 0; i < count; ++i) {
                    UINT len = DragQueryFileW(drop, i, nullptr, 0);
                    std::wstring s(len + 1, L'\0');
                    DragQueryFileW(drop, i, s.data(), len + 1);
                    s.resize(len);
                    paths.push_back(std::move(s));
                }
                GlobalUnlock(medium.hGlobal);
            }
            ReleaseStgMedium(&medium);

            POINT p{pt.x, pt.y};
            ScreenToClient(pane_->hwnd(), &p);
            if (pane_->IsOverBookmarkRow(p)) {
                pane_->AddBookmarksFromPaths(paths);
                *effect = DROPEFFECT_COPY;
            } else {
                std::wstring dest = pane_->CurrentPath();
                if (!dest.empty() && !paths.empty()) {
                    if (keyState & MK_SHIFT) RunMove(paths, dest, pane_->hwnd());
                    else RunCopy(paths, dest, pane_->hwnd());
                    *effect = (keyState & MK_SHIFT) ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
                }
            }
        }
        hasFiles_ = false;
        return S_OK;
    }

private:
    DWORD Effect(DWORD keyState) const {
        if (!hasFiles_) return DROPEFFECT_NONE;
        return (keyState & MK_SHIFT) ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
    }

    PaneView* pane_;
    ULONG refs_ = 1;
    bool hasFiles_ = false;
};

// ======================= 생성 / 소멸 =======================

bool PaneView::RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &PaneView::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kPaneClass;
    return RegisterClassExW(&wc) != 0;
}

PaneView* PaneView::Create(HWND parent) {
    auto* pane = new PaneView();
    HINSTANCE inst = GetModuleHandleW(nullptr);
    pane->hwnd_ = CreateWindowExW(0, kPaneClass, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 10, 10, parent, nullptr, inst, pane);
    if (!pane->hwnd_) {
        delete pane;
        return nullptr;
    }
    pane->files_.reset(FileView::Create(pane->hwnd_, pane));
    pane->edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, pane->hwnd_, nullptr, inst, nullptr);
    SendMessageW(pane->edit_, WM_SETFONT, reinterpret_cast<WPARAM>(ui::font), TRUE);
    SetWindowSubclass(pane->edit_, &PaneView::AddressProc, 1, reinterpret_cast<DWORD_PTR>(pane));

    pane->tip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT,
                                 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, pane->hwnd_, nullptr, inst, nullptr);
    if (pane->tip_) {
        TOOLINFOW ti{};
        ti.cbSize = sizeof ti;
        ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = pane->hwnd_;
        ti.uId = 1;
        ti.lpszText = LPSTR_TEXTCALLBACKW;
        SendMessageW(pane->tip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
        SendMessageW(pane->tip_, TTM_SETMAXTIPWIDTH, 0, 700);
    }
    pane->dropTarget_ = new PaneDropTarget(pane);
    RegisterDragDrop(pane->hwnd_, pane->dropTarget_);
    pane->Layout();
    return pane;
}

PaneView::~PaneView() {
    Dispose();
    if (hwnd_) {
        RevokeDragDrop(hwnd_);
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
    }
    if (dropTarget_) dropTarget_->Release();
    files_.reset();
    if (hwnd_) DestroyWindow(hwnd_);
    if (groupMenu_) DestroyMenu(groupMenu_);
    if (memDc_) {
        if (backOld_) SelectObject(memDc_, backOld_);
        DeleteDC(memDc_);
    }
    if (backBmp_) DeleteObject(backBmp_);
}

void PaneView::Dispose() {
    if (disposed_) return;
    disposed_ = true;
    StopWatcher();
    for (auto& t : tabs_) t->Dispose();
}

LRESULT CALLBACK PaneView::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PaneView* self;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<PaneView*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<PaneView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->HandleMessage(msg, wParam, lParam);
}

LRESULT CALLBACK PaneView::AddressProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR ref) {
    auto* pane = reinterpret_cast<PaneView*>(ref);
    switch (msg) {
        case WM_KEYDOWN:
            if (wParam == VK_RETURN) {
                pane->CommitAddress();
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                pane->ExitAddress();
                pane->FocusFiles();
                return 0;
            }
            break;
        case WM_CHAR:
            if (wParam == VK_RETURN || wParam == VK_ESCAPE) return 0;   // 경고음 방지
            break;
        case WM_KILLFOCUS: {
            LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
            if (pane->editing_) pane->ExitAddress();
            return r;
        }
        case WM_NCDESTROY: RemoveWindowSubclass(hwnd, &PaneView::AddressProc, 1); break;
        default: break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ======================= 탭 모델 =======================

std::wstring PaneView::HomePath() const {
    std::wstring home = KnownFolder(FOLDERID_Profile);
    return home.empty() ? L"C:\\" : home;
}

std::shared_ptr<FolderTab> PaneView::SelectedTab() const {
    return (selected_ >= 0 && selected_ < static_cast<int>(tabs_.size())) ? tabs_[static_cast<size_t>(selected_)] : nullptr;
}

std::shared_ptr<FolderTab> PaneView::MakeTab(const std::wstring& path, ViewMode mode) {
    auto tab = std::make_shared<FolderTab>(path, mode);
    tab->SetListener([this](FolderTab& t, unsigned what) { OnTabChanged(t, what); });
    tab->SetStateProvider([this, weak = std::weak_ptr<FolderTab>(tab)](FolderViewState& out) {
        auto sel = SelectedTab();
        auto sp = weak.lock();
        if (files_ && sel && sp && sel == sp) {
            files_->GetViewState(out);
        }
    });
    return tab;
}

void PaneView::NewTab() {
    auto cur = SelectedTab();
    auto tab = MakeTab(cur ? cur->Path() : HomePath(), cur ? cur->Mode() : ViewMode::Details);
    tab->SetColor(CLR_INVALID);
    tabs_.push_back(tab);
    tabsDirty_ = true;
    SelectTab(static_cast<int>(tabs_.size()) - 1);
}

void PaneView::CloseTab(std::shared_ptr<FolderTab> tab) {
    if (!tab) return;
    auto keepAlive = tab;
    int idx = -1;
    for (size_t i = 0; i < tabs_.size(); ++i)
        if (tabs_[i] == keepAlive) idx = static_cast<int>(i);
    if (idx < 0) return;
    keepAlive->Dispose();
    const bool wasSelected = idx == selected_;
    if (tabs_.size() == 1) {   // 마지막 탭을 닫으면 홈 탭 하나를 새로 연다
        tabs_.push_back(MakeTab(HomePath(), ViewMode::Details));
        tabs_.erase(tabs_.begin() + idx);
        tabsDirty_ = true;
        SelectTab(0, true);
        return;
    }
    tabs_.erase(tabs_.begin() + idx);
    tabsDirty_ = true;
    if (wasSelected) {
        selected_ = -1;
        SelectTab(std::min(idx, static_cast<int>(tabs_.size()) - 1), true);
    } else {
        if (selected_ > idx) --selected_;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void PaneView::SelectTab(int index, bool force) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return;
    if (index == selected_ && !force) return;
    ExitAddress();
    selected_ = index;
    auto tab = tabs_[static_cast<size_t>(index)];
    files_->SetTab(tab);
    crumbsDirty_ = tabsDirty_ = true;
    EnsureTabVisible(index);
    StartWatcher();
    tab->Refresh();   // 이미 표시 중인 목록은 그대로 두고, 바뀐 경우에만 조용히 갱신된다
    if (cb.modeChanged) cb.modeChanged();
    if (cb.searchChanged) cb.searchChanged();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PaneView::SelectNextTab(int step) {
    if (tabs_.empty()) return;
    int n = static_cast<int>(tabs_.size());
    SelectTab(((selected_ + step) % n + n) % n);
}

void PaneView::Restore(const PaneState& state) {
    for (auto& t : tabs_) t->Dispose();
    tabs_.clear();
    selected_ = -1;
    for (const auto& ts : state.tabs) {
        std::wstring p, err;
        if (!path::TryNormalize(ts.path, L"", p, &err)) continue;
        tabs_.push_back(MakeTab(p, ts.mode));
    }
    if (tabs_.empty()) tabs_.push_back(MakeTab(HomePath(), ViewMode::Details));
    tabsDirty_ = true;
    SelectTab(std::clamp(state.selectedIndex, 0, static_cast<int>(tabs_.size()) - 1), true);
}

PaneState PaneView::Capture() const {
    PaneState state;
    for (const auto& t : tabs_) state.tabs.push_back({t->Path(), t->Mode()});
    state.selectedIndex = std::max(0, selected_);
    return state;
}

void PaneView::ExecuteSearch(const TabSearchState& state) {
    if (auto t = SelectedTab()) t->ExecuteSearch(state);
}

void PaneView::CancelSearch() {
    if (auto t = SelectedTab()) t->CancelSearch();
}

TabSearchState PaneView::GetCurrentSearchState() const {
    if (auto t = SelectedTab()) return t->GetSearchState();
    return {};
}

void PaneView::OnTabChanged(FolderTab& tab, unsigned what) {
    if (disposed_) return;
    auto sel = SelectedTab();
    const bool isSelected = sel && sel.get() == &tab;
    if (what & kChangePath) tabsDirty_ = true;
    if (isSelected) {
        if (what & (kChangeEntries | kChangeSort | kChangeMode)) files_->OnTabChanged(what);
        if (what & kChangePath) {
            crumbsDirty_ = true;
            if (!EqualsNoCase(watchedPath_, tab.Path())) StartWatcher();
            if (cb.searchChanged) cb.searchChanged();
        }
        if ((what & kChangeMode) && cb.modeChanged) cb.modeChanged();
        InvalidateRect(hwnd_, nullptr, FALSE);
    } else if (what & (kChangePath | kChangeMode)) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void PaneView::SetModeOfSelected(ViewMode mode) {
    if (auto t = SelectedTab()) t->SetMode(mode);
}

void PaneView::SetActive(bool active) {
    if (active_ == active) return;
    active_ = active;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PaneView::SetPaneVisible(bool visible) {
    visible_ = visible;
    ShowWindow(hwnd_, visible ? SW_SHOW : SW_HIDE);
    if (visible) {
        StartWatcher();
        Refresh();
    } else {
        StopWatcher();
    }
}

void PaneView::ApplyDpi() {
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(ui::font), TRUE);
    tabsDirty_ = bmDirty_ = crumbsDirty_ = true;
    Layout();
    if (files_) files_->ApplyMetrics();
}

void PaneView::ApplyTheme() {
    InvalidateRect(hwnd_, nullptr, TRUE);
    if (files_) files_->RepaintNow();
}

void PaneView::OnBookmarksChanged() {
    bmDirty_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ======================= 감시 =======================

void PaneView::StartWatcher() {
    StopWatcher();
    if (!visible_ || disposed_) return;
    auto tab = SelectedTab();
    if (!tab) return;
    watchedPath_ = tab->Path();
    watcher_.Start(watchedPath_, g_mainWnd, WM_FT_FSCHANGE, watchGen_, reinterpret_cast<LPARAM>(hwnd_));
}

void PaneView::StopWatcher() {
    ++watchGen_;
    if (refreshTimerOn_) {
        KillTimer(hwnd_, kRefreshTimer);
        refreshTimerOn_ = false;
    }
    watchPending_ = false;
    watcher_.Stop();
    watchedPath_.clear();
}

void PaneView::OnFsChange(uintptr_t generation) {
    if (disposed_ || generation != watchGen_) return;
    watchPending_ = true;
    if (!refreshTimerOn_) {
        SetTimer(hwnd_, kRefreshTimer, 450, nullptr);   // 연속 변경을 한 번의 갱신으로 묶는다
        refreshTimerOn_ = true;
    }
}

void PaneView::Refresh() {
    if (auto t = SelectedTab()) t->Refresh();
}

// ======================= 동작 =======================

void PaneView::NavigateSelected(const std::wstring& target) {
    if (auto t = SelectedTab()) t->Navigate(target);
}

void PaneView::Up() {
    auto t = SelectedTab();
    if (!t) return;
    t->Up();
}

void PaneView::ReportOpenFailure(const std::wstring& target) {
    ui::ShowError(hwnd_, L"'" + path::Label(target) + L"'에 액세스할 수 없습니다.\n\n액세스가 거부되었거나 사용 권한이 없습니다.", L"폴더 열기");
}

void PaneView::OpenSelected() {
    auto entries = SelectedEntries();
    auto tab = SelectedTab();
    if (entries.empty() || !tab) return;
    const FileEntry& e = entries[0];
    const std::wstring full = files_->FullPath(e);
    if (e.IsDir()) {
        std::wstring target = (e.attrs & kAttrReparsePoint) ? ResolveDirectoryLink(full) : full;
        tab->Navigate(target, [this, target](bool ok) {
            if (!ok && !disposed_) ReportOpenFailure(target);
        });
        return;
    }
    std::wstring lnkTarget = ResolveShortcutTarget(full);
    if (!lnkTarget.empty() && IsDirectoryPath(lnkTarget)) {
        tab->Navigate(lnkTarget, [this, lnkTarget](bool ok) {
            if (!ok && !disposed_) ReportOpenFailure(lnkTarget);
        });
        return;
    }
    std::wstring error;
    if (!OpenWithShell(hwnd_, full, &error)) ui::ShowError(hwnd_, L"열 수 없습니다: " + error, L"파일 열기");
}

void PaneView::SelectAfterRefresh(const std::wstring& name) {
    if (files_) files_->SelectOnlyByName(name);
}

void PaneView::CreateFolder() {
    auto tab = SelectedTab();
    if (!tab) return;
    std::wstring name = L"새 폴더";
    auto validate = [](const std::wstring& input) -> std::wstring {
        bool any = false;
        size_t pos = 0;
        while (pos <= input.size()) {
            size_t e = input.find_first_of(L"\\/", pos);
            if (e == std::wstring::npos) e = input.size();
            std::wstring seg = input.substr(pos, e - pos);
            pos = e + 1;
            if (seg.empty()) continue;
            any = true;
            std::wstring err = path::ValidateName(seg);
            if (!err.empty()) return err;
        }
        return any ? std::wstring() : std::wstring(L"폴더 이름을 입력하세요.");
    };
    if (!InputDialog(hwnd_, L"새 폴더", L"새 폴더 이름 ('\\' 로 구분하면 하위 폴더까지 만듭니다)", name, 0, static_cast<int>(name.size()), L"만들기", validate))
        return;
    std::vector<std::wstring> segments;
    size_t pos = 0;
    while (pos <= name.size()) {
        size_t e = name.find_first_of(L"\\/", pos);
        if (e == std::wstring::npos) e = name.size();
        if (e > pos) segments.push_back(name.substr(pos, e - pos));
        pos = e + 1;
    }
    if (segments.empty()) return;
    std::wstring target = tab->Path();
    for (const auto& s : segments) target = path::Combine(target, s);
    std::wstring error;
    if (!CreateDirectories(hwnd_, target, &error)) {
        if (!error.empty()) ui::ShowError(hwnd_, L"폴더를 만들 수 없습니다: " + error, L"새 폴더");
        return;
    }
    const std::wstring first = segments[0];
    tab->Refresh([this, first](bool ok) {
        if (ok && !disposed_) SelectAfterRefresh(first);
    });
}

// ---- 북마크 ----

void PaneView::AddBookmarksFromPaths(const std::vector<std::wstring>& paths) {
    auto& settings = SettingsService::Get();
    bool changed = false;
    std::wstring firstError;
    for (const auto& p : paths) {
        std::wstring normalized, err;
        if (!path::TryNormalize(p, L"", normalized, &err) || !IsDirectoryPath(normalized)) continue;   // 폴더만 북마크
        if (settings.AddBookmark(normalized, &err)) changed = true;
        else if (!err.empty() && firstError.empty()) firstError = err;
    }
    if (changed) {
        std::wstring error;
        if (!settings.Save(&error)) ui::ShowError(hwnd_, L"설정을 저장하지 못했습니다: " + error);
        if (cb.bookmarksChanged) cb.bookmarksChanged();
    }
    if (!firstError.empty()) ui::ShowError(hwnd_, firstError);
}

void PaneView::AddSelectedBookmarks() {
    std::vector<std::wstring> dirs;
    for (const auto& e : SelectedEntries())
        if (e.IsDir()) dirs.push_back(files_->FullPath(e));
    AddBookmarksFromPaths(dirs);
}

void PaneView::AddCurrentBookmark() {
    if (auto t = SelectedTab()) AddBookmarksFromPaths({t->Path()});
}

void PaneView::EditBookmark(int index) {
    auto& settings = SettingsService::Get();
    if (index < 0 || index >= static_cast<int>(settings.state.bookmarks.size())) return;
    Bookmark copy = settings.state.bookmarks[static_cast<size_t>(index)];
    if (!BookmarkDialog(hwnd_, copy)) return;
    settings.state.bookmarks[static_cast<size_t>(index)] = copy;
    std::wstring error;
    if (!settings.Save(&error)) ui::ShowError(hwnd_, L"설정을 저장하지 못했습니다: " + error);
    if (cb.bookmarksChanged) cb.bookmarksChanged();
}

void PaneView::DeleteBookmark(int index) {
    auto& settings = SettingsService::Get();
    if (index < 0 || index >= static_cast<int>(settings.state.bookmarks.size())) return;
    settings.state.bookmarks.erase(settings.state.bookmarks.begin() + index);
    std::wstring error;
    if (!settings.Save(&error)) ui::ShowError(hwnd_, L"설정을 저장하지 못했습니다: " + error);
    if (cb.bookmarksChanged) cb.bookmarksChanged();
}

// ---- 폴더 그룹 ----

void PaneView::AddFolderGroupFromCurrent() {
    FolderGroup group;
    for (const auto& t : tabs_) {
        if (Trim(t->Path()).empty()) continue;
        group.paths.push_back(t->Path());
        group.modes.push_back(t->Mode());
        group.colors.push_back(t->Color());
    }
    if (group.paths.empty()) return;
    std::wstring trimmed = group.paths[0];
    while (!trimmed.empty() && trimmed.back() == L'\\') trimmed.pop_back();
    group.name = path::FileName(trimmed);
    if (Trim(group.name).empty()) group.name = group.paths[0];
    auto cur = SelectedTab();
    group.mode = cur ? cur->Mode() : ViewMode::Details;
    auto& settings = SettingsService::Get();
    settings.state.folderGroups.push_back(std::move(group));
    std::wstring error;
    if (!settings.Save(&error)) ui::ShowError(hwnd_, L"설정을 저장하지 못했습니다: " + error);
}

void PaneView::LoadFolderGroup(const FolderGroup& group) {
    if (group.paths.empty()) return;
    // 첫 탭만 남기고 모두 닫은 뒤, 그룹의 폴더를 차례로 연다.
    while (tabs_.size() > 1) {
        auto tabToClose = tabs_.back();
        CloseTab(tabToClose);
    }
    auto first = SelectedTab();
    if (!first) return;
    first->SetMode(!group.modes.empty() ? group.modes[0] : group.mode);
    first->Navigate(group.paths[0]);
    if (!group.colors.empty()) first->SetColor(group.colors[0]);
    for (size_t i = 1; i < group.paths.size(); ++i) {
        ViewMode m = i < group.modes.size() ? group.modes[i] : group.mode;
        auto tab = MakeTab(group.paths[i], m);
        if (i < group.colors.size()) tab->SetColor(group.colors[i]);
        tabs_.push_back(tab);
        tabsDirty_ = true;
        SelectTab(static_cast<int>(tabs_.size()) - 1);
    }
}

void PaneView::ShowFolderGroupMenu() {
    if (groupMenu_) {
        DestroyMenu(groupMenu_);
        groupMenu_ = nullptr;
    }
    groupMenu_ = CreatePopupMenu();
    AppendMenuW(groupMenu_, MF_STRING, 1, L"폴더 그룹 추가");
    AppendMenuW(groupMenu_, MF_STRING, 2, L"폴더 그룹 수정");
    const auto& groups = SettingsService::Get().state.folderGroups;
    if (!groups.empty()) AppendMenuW(groupMenu_, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < groups.size(); ++i) AppendMenuW(groupMenu_, MF_STRING, static_cast<UINT_PTR>(100 + i), groups[i].name.c_str());
    // 위치(인덱스)로 알림을 받아야 항목 오른쪽 클릭(WM_MENURBUTTONUP)을 처리할 수 있다.
    MENUINFO mi{};
    mi.cbSize = sizeof mi;
    mi.fMask = MIM_STYLE;
    mi.dwStyle = MNS_NOTIFYBYPOS;
    SetMenuInfo(groupMenu_, &mi);
    POINT pt{groupBtn_.left, groupBtn_.bottom};
    ClientToScreen(hwnd_, &pt);
    TrackPopupMenuEx(groupMenu_, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, hwnd_, nullptr);
}

void PaneView::OnGroupMenuCommand(int index) {
    if (index < 0) return;
    if (index == 0) {
        AddFolderGroupFromCurrent();
    } else if (index == 1) {
        FolderGroupManagerDialog(GetAncestor(hwnd_, GA_ROOT));
    } else if (index >= 3) {
        const auto& groups = SettingsService::Get().state.folderGroups;
        size_t gi = static_cast<size_t>(index - 3);
        if (gi < groups.size()) {
            FolderGroup copy = groups[gi];
            LoadFolderGroup(copy);
        }
    }
}

void PaneView::OnGroupMenuRightClick(int index) {
    if (index < 3) return;
    auto& settings = SettingsService::Get();
    size_t gi = static_cast<size_t>(index - 3);
    if (gi >= settings.state.folderGroups.size()) return;
    HMENU sub = CreatePopupMenu();
    AppendMenuW(sub, MF_STRING, kCmdGroupEdit, L"폴더 그룹 수정");
    AppendMenuW(sub, MF_STRING, kCmdGroupDelete, L"폴더 그룹 삭제");
    POINT pt;
    GetCursorPos(&pt);
    UINT cmd = static_cast<UINT>(TrackPopupMenuEx(sub, TPM_RETURNCMD | TPM_RECURSE | TPM_RIGHTBUTTON, pt.x, pt.y, hwnd_, nullptr));
    DestroyMenu(sub);
    if (cmd == 0) return;
    EndMenu();   // 부모 메뉴를 닫는다
    HWND owner = GetAncestor(hwnd_, GA_ROOT);
    if (cmd == kCmdGroupEdit) {
        FolderGroup copy = settings.state.folderGroups[gi];
        if (FolderGroupEditDialog(owner, copy)) {
            settings.state.folderGroups[gi] = copy;
            std::wstring error;
            if (!settings.Save(&error)) ui::ShowError(owner, L"설정을 저장하지 못했습니다: " + error);
        }
    } else if (cmd == kCmdGroupDelete) {
        if (MessageBoxW(owner, (L"폴더 그룹 '" + settings.state.folderGroups[gi].name + L"'을(를) 삭제하시겠습니까?").c_str(), L"폴더 그룹 삭제",
                        MB_YESNO | MB_ICONQUESTION) == IDYES) {
            settings.state.folderGroups.erase(settings.state.folderGroups.begin() + static_cast<std::ptrdiff_t>(gi));
            std::wstring error;
            if (!settings.Save(&error)) ui::ShowError(owner, L"설정을 저장하지 못했습니다: " + error);
        }
    }
}

// ---- 파일 목록 메뉴 ----

void PaneView::ShowFilesMenu(POINT screen) {
    auto entries = SelectedEntries();
    auto tab = SelectedTab();
    bool anyDir = false;
    for (const auto& e : entries) anyDir = anyDir || e.IsDir();
    HMENU menu = CreatePopupMenu();
    auto flag = [](bool enabled) { return static_cast<UINT>(MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED)); };
    AppendMenuW(menu, flag(!entries.empty()), kCmdOpen, L"열기");
    AppendMenuW(menu, MF_STRING, kCmdNewFolder, L"새 폴더");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, flag(!entries.empty()), kCmdCopy, L"복사\tCtrl+C");
    AppendMenuW(menu, flag(ClipboardHasFiles()), kCmdPaste, L"붙여넣기\tCtrl+V");
    AppendMenuW(menu, flag(entries.size() == 1), kCmdRename, L"이름 바꾸기\tF2");
    AppendMenuW(menu, flag(!entries.empty()), kCmdDelete, L"삭제\tDel");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, flag(anyDir), kCmdBookmarkSel, L"북마크에 추가");
    AppendMenuW(menu, MF_STRING, kCmdBookmarkCur, L"현재 폴더를 북마크에 추가");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    HMENU view = CreatePopupMenu();
    ViewMode mode = tab ? tab->Mode() : ViewMode::Details;
    AppendMenuW(view, MF_STRING | (mode == ViewMode::Details ? MF_CHECKED : 0), kCmdViewDetails, L"자세히");
    AppendMenuW(view, MF_STRING | (mode == ViewMode::LargeIcons ? MF_CHECKED : 0), kCmdViewLarge, L"큰 아이콘 (128px)");
    AppendMenuW(view, MF_STRING | (mode == ViewMode::MediumIcons ? MF_CHECKED : 0), kCmdViewMedium, L"보통 아이콘 (64px)");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"보기");
    AppendMenuW(menu, MF_STRING, kCmdRefresh, L"새로 고침\tF5");

    UINT cmd = static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN, screen.x, screen.y, hwnd_, nullptr));
    DestroyMenu(menu);   // 하위 메뉴도 함께 삭제된다
    switch (cmd) {
        case kCmdOpen: if (cb.action) cb.action(this, FileAction::Open); break;
        case kCmdNewFolder: CreateFolder(); break;
        case kCmdCopy: if (cb.action) cb.action(this, FileAction::Copy); break;
        case kCmdPaste: if (cb.action) cb.action(this, FileAction::Paste); break;
        case kCmdRename: if (cb.action) cb.action(this, FileAction::Rename); break;
        case kCmdDelete:
            if (cb.action) cb.action(this, GetKeyState(VK_SHIFT) < 0 ? FileAction::PermanentDelete : FileAction::Delete);
            break;
        case kCmdBookmarkSel: AddSelectedBookmarks(); break;
        case kCmdBookmarkCur: AddCurrentBookmark(); break;
        case kCmdViewDetails: SetModeOfSelected(ViewMode::Details); break;
        case kCmdViewLarge: SetModeOfSelected(ViewMode::LargeIcons); break;
        case kCmdViewMedium: SetModeOfSelected(ViewMode::MediumIcons); break;
        case kCmdRefresh: Refresh(); break;
        default: break;
    }
}

// ---- FileView::Host ----

void PaneView::FvActivate() {
    if (cb.activated) cb.activated(this);
}
void PaneView::FvSelectionChanged() { InvalidateRect(hwnd_, &statusRect_, FALSE); }
void PaneView::FvOpen() {
    if (cb.action) cb.action(this, FileAction::Open);
}
void PaneView::FvContextMenu(POINT screen) { ShowFilesMenu(screen); }
void PaneView::FvSortRequested(SortKey key) {
    auto tab = SelectedTab();
    if (!tab) return;
    // 같은 열을 오름차순으로 보고 있을 때만 내림차순으로 바꾼다(WPF 버전과 같은 규칙)
    bool desc = tab->SortColumn() == key && !tab->SortDescending();
    tab->Sort(key, desc);
}

void PaneView::FvDropFiles(const std::vector<std::wstring>& paths, const std::wstring& targetDir, bool isMove) {
    if (paths.empty() || targetDir.empty()) return;
    if (isMove) {
        RunMove(paths, targetDir, hwnd_);
    } else {
        RunCopy(paths, targetDir, hwnd_);
    }
}

}  // namespace ft
