#include "FolderTab.h"

#include "FsServices.h"
#include "Workers.h"

namespace ft {

namespace {
ThreadPool& DirPool() {
    static ThreadPool* pool = new ThreadPool(3);   // 프로세스 종료까지 유지
    return *pool;
}
}  // namespace

FolderTab::FolderTab(std::wstring path, ViewMode mode)
    : path_(std::move(path)), mode_(mode), entries_(std::make_shared<const std::vector<FileEntry>>()) {}

void FolderTab::SetMode(ViewMode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    Notify(kChangeMode);
}

void FolderTab::Sort(SortKey key, bool descending) {
    sortKey_ = key;
    sortDesc_ = descending;
    auto copy = std::make_shared<std::vector<FileEntry>>(*entries_);
    SortEntries(*copy, key, descending);
    entries_ = copy;
    Notify(kChangeSort | kChangeEntries);
}

void FolderTab::Navigate(const std::wstring& path, Done done) {
    Load(path, 0, std::move(done), L"", {});
}

void FolderTab::Refresh(Done done) {
    if (searchState_.active) Load(path_, 2, std::move(done), L"", searchState_);
    else Load(path_, 2, std::move(done), L"", {});
}

void FolderTab::Load(const std::wstring& input, int direction, Done done, const std::wstring& childName) {
    Load(input, direction, std::move(done), childName, {});
}

void FolderTab::Back(Done done) {
    if (const auto* p = history_.BackEntry()) {
        std::wstring target = p->path;
        TabSearchState search = p->search;
        Load(target, -1, std::move(done), L"", search);
    } else if (done) {
        done(false);
    }
}

void FolderTab::Forward(Done done) {
    if (const auto* p = history_.ForwardEntry()) {
        std::wstring target = p->path;
        TabSearchState search = p->search;
        Load(target, 1, std::move(done), L"", search);
    } else if (done) {
        done(false);
    }
}

void FolderTab::ExecuteSearch(const TabSearchState& state) {
    if (!searchState_.active) {
        normalEntries_ = entries_;
    }
    searchState_ = state;
    searchState_.ApplyGlobalOptions();
    searchState_.active = true;
    Load(path_, 0, nullptr, L"", searchState_);
}

void FolderTab::CancelSearch() {
    searchState_ = {};
    searchState_.active = false;
    if (normalEntries_ && !normalEntries_->empty()) {
        entries_ = normalEntries_;
        normalEntries_.reset();
        status_ = FormatThousands(static_cast<int64_t>(entries_->size())) + L"개 항목";
        history_.Visit(path_, {});
        Notify(kChangeEntries | kChangePath | kChangeBusy);
    } else {
        Load(path_, 0, nullptr, L"", {});
    }
}

void FolderTab::Up(Done done) {
    std::wstring parent = path::Parent(path_);
    if (parent.empty()) {
        if (done) done(false);
        return;
    }
    std::wstring child = path::Label(path_);
    Load(parent, -2, std::move(done), child);
}

void FolderTab::SaveViewState(const std::wstring& path, const FolderViewState& state) {
    if (path.empty()) return;
    pathStates_[path] = state;
}

bool FolderTab::GetViewState(const std::wstring& path, FolderViewState& out) const {
    auto it = pathStates_.find(path);
    if (it != pathStates_.end()) {
        out = it->second;
        return true;
    }
    return false;
}

void FolderTab::Dispose() {
    disposed_ = true;
    if (cancel_) cancel_->store(true);
    pendingDone_ = nullptr;
    listener_ = nullptr;
    stateProvider_ = nullptr;
}

void FolderTab::Finish(bool ok) {
    Done d = std::move(pendingDone_);
    pendingDone_ = nullptr;
    if (d) d(ok);
}

void FolderTab::Load(const std::wstring& input, int direction, Done done, const std::wstring& childName, const TabSearchState& search) {
    if (disposed_) {
        if (done) done(false);
        return;
    }

    // 폴더를 떠나기 전에 현재 상태(스크롤·선택)를 저장한다.
    if (direction != 2 && !path_.empty() && stateProvider_) {
        FolderViewState vs;
        stateProvider_(vs);
        pathStates_[path_] = vs;
        history_.SaveCurrentState(vs);
    }

    // 새 이동 요청은 이전 조회를 취소한다.
    if (cancel_) cancel_->store(true);
    if (pendingDone_) {
        Done superseded = std::move(pendingDone_);
        pendingDone_ = nullptr;
        superseded(false);
    }
    pendingDone_ = std::move(done);
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    ++seq_;
    busy_ = true;
    // 새로 고침(감시·탭 전환)은 상태 문구를 매번 바꾸지 않는다.
    if (direction != 2) status_ = search.active ? L"검색 중…" : L"폴더를 읽는 중…";
    Notify(kChangeBusy);

    std::wstring normalized, error;
    if (!path::TryNormalize(input, path_, normalized, &error)) {
        busy_ = false;
        status_ = L"열기 실패: " + error;
        Notify(kChangeBusy);
        Finish(false);
        return;
    }

    std::weak_ptr<FolderTab> weak = weak_from_this();
    std::shared_ptr<std::atomic<bool>> cancel = cancel_;
    uint64_t seq = seq_;
    SortKey key = sortKey_;
    bool desc = sortDesc_;
    DirPool().Post([weak, cancel, seq, direction, normalized, childName, search, key, desc]() {
        auto* r = new DirResult();
        r->tab = weak;
        r->seq = seq;
        r->direction = direction;
        r->path = normalized;
        r->childName = childName;
        r->search = search;
        r->isSearch = search.active;
        r->sortKey = key;
        r->sortDesc = desc;
        if (search.active) {
            r->ok = SearchFiles(normalized, search, r->entries, r->error, *cancel);
        } else {
            r->ok = ReadDirectory(normalized, r->entries, r->error, *cancel);
        }
        if (r->ok) SortEntries(r->entries, key, desc);   // 정렬도 워커에서: UI 스레드는 교체만 한다
        if (cancel->load() || !g_mainWnd || !PostMessageW(g_mainWnd, WM_FT_DIRREAD, 0, reinterpret_cast<LPARAM>(r))) delete r;
    });
}

void FolderTab::OnResult(DirResult& r) {
    if (disposed_ || r.seq != seq_) return;
    busy_ = false;
    if (!r.ok) {
        status_ = L"열기 실패: " + r.error;
        Notify(kChangeBusy);
        Finish(false);
        return;
    }
    bool historyOk = true;
    if (r.direction == -1) historyOk = history_.GoBack(r.path);
    else if (r.direction == 1) historyOk = history_.GoForward(r.path);
    else if (r.direction == 0 || !history_.Current()) history_.Visit(r.path, r.search);
    else if (r.direction == -2) history_.Visit(r.path, {});
    if (!historyOk) {
        Notify(kChangeBusy);
        Finish(false);
        return;
    }

    searchState_ = r.search;

    const std::wstring oldPath = path_;
    const bool pathChanged = !EqualsNoCase(path_, r.path);
    path_ = r.path;
    if (r.sortKey != sortKey_ || r.sortDesc != sortDesc_) SortEntries(r.entries, sortKey_, sortDesc_);

    // 복원할 상태 결정 (뒤로가기/앞으로가기/상위폴더)
    if (pathChanged) {
        if (r.direction == -1 || r.direction == 1) {
            FolderViewState st;
            bool has = false;
            if (const auto* s = history_.CurrentState()) {
                if (s->scrollY != 0 || !s->selection.empty() || !s->focus.empty()) {
                    st = *s;
                    has = true;
                }
            }
            if (!has) {
                auto it = pathStates_.find(r.path);
                if (it != pathStates_.end()) {
                    st = it->second;
                    has = true;
                }
            }
            if (has) {
                pendingRestoreState_ = st;
                savedSelection = st.selection;
                savedFocus = st.focus;
                savedScroll = st.scrollY;
            } else {
                pendingRestoreState_.reset();
                savedSelection.clear();
                savedFocus.clear();
                savedScroll = 0;
            }
        } else if (r.direction == -2) {
            FolderViewState st;
            bool has = false;
            auto it = pathStates_.find(r.path);
            if (it != pathStates_.end()) {
                st = it->second;
                has = true;
            }
            if (has) {
                if (st.selection.empty() && !r.childName.empty()) {
                    st.selection.push_back(r.childName);
                    st.focus = r.childName;
                }
                pendingRestoreState_ = st;
                savedSelection = st.selection;
                savedFocus = st.focus;
                savedScroll = st.scrollY;
            } else {
                if (!r.childName.empty()) {
                    st.selection = { r.childName };
                    st.focus = r.childName;
                    st.scrollY = -1;
                    pendingRestoreState_ = st;
                    savedSelection = st.selection;
                    savedFocus = st.focus;
                    savedScroll = 0;
                } else {
                    pendingRestoreState_.reset();
                    savedSelection.clear();
                    savedFocus.clear();
                    savedScroll = 0;
                }
            }
        } else if (r.direction == 0) {
            FolderViewState st;
            bool has = false;
            if (!oldPath.empty() && path::IsSameOrDescendant(oldPath, r.path) && !EqualsNoCase(oldPath, r.path)) {
                auto it = pathStates_.find(r.path);
                if (it != pathStates_.end()) {
                    st = it->second;
                    has = true;
                }
            }
            if (has) {
                pendingRestoreState_ = st;
                savedSelection = st.selection;
                savedFocus = st.focus;
                savedScroll = st.scrollY;
            } else {
                pendingRestoreState_.reset();
                savedSelection.clear();
                savedFocus.clear();
                savedScroll = 0;
            }
        } else {
            pendingRestoreState_.reset();
        }
    }

    unsigned what = kChangeBusy;
    if (r.direction != 2) what |= kChangePath;   // 뒤로/앞으로 버튼 상태
    // 같은 폴더이고 내용이 완전히 같으면 목록을 건드리지 않는다(깜빡임·스크롤·선택 유지).
    const bool same = !pathChanged && !r.isSearch && SameListing(*entries_, r.entries);
    if (!same) {
        status_ = FormatThousands(static_cast<int64_t>(r.entries.size())) + (r.isSearch ? L"개 항목 찾음" : L"개 항목");
        entries_ = std::make_shared<const std::vector<FileEntry>>(std::move(r.entries));
        what |= kChangeEntries;
    } else {
        status_ = FormatThousands(static_cast<int64_t>(entries_->size())) + (r.isSearch ? L"개 항목 찾음" : L"개 항목");
    }
    if (pathChanged || r.isSearch) what |= kChangePath | kChangeEntries;
    Notify(what);
    Finish(true);
}

void DispatchDirResult(DirResult* result) {
    std::unique_ptr<DirResult> owned(result);
    if (auto tab = owned->tab.lock()) tab->OnResult(*owned);
}

}  // namespace ft
