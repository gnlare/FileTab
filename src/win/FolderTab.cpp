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

void FolderTab::Back(Done done) {
    if (const std::wstring* p = history_.Back()) {
        std::wstring target = *p;
        Load(target, -1, std::move(done));
    } else if (done) {
        done(false);
    }
}

void FolderTab::Forward(Done done) {
    if (const std::wstring* p = history_.Forward()) {
        std::wstring target = *p;
        Load(target, 1, std::move(done));
    } else if (done) {
        done(false);
    }
}

void FolderTab::Dispose() {
    disposed_ = true;
    if (cancel_) cancel_->store(true);
    pendingDone_ = nullptr;
    listener_ = nullptr;
}

void FolderTab::Finish(bool ok) {
    Done d = std::move(pendingDone_);
    pendingDone_ = nullptr;
    if (d) d(ok);
}

void FolderTab::Load(const std::wstring& input, int direction, Done done) {
    if (disposed_) {
        if (done) done(false);
        return;
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
    if (direction != 2) status_ = L"폴더를 읽는 중…";
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
    DirPool().Post([weak, cancel, seq, direction, normalized, key, desc]() {
        auto* r = new DirResult();
        r->tab = weak;
        r->seq = seq;
        r->direction = direction;
        r->path = normalized;
        r->sortKey = key;
        r->sortDesc = desc;
        r->ok = ReadDirectory(normalized, r->entries, r->error, *cancel);
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
    else if (r.direction == 0 || !history_.Current()) history_.Visit(r.path);
    if (!historyOk) {
        Notify(kChangeBusy);
        Finish(false);
        return;
    }

    const bool pathChanged = !EqualsNoCase(path_, r.path);
    path_ = r.path;
    if (r.sortKey != sortKey_ || r.sortDesc != sortDesc_) SortEntries(r.entries, sortKey_, sortDesc_);

    unsigned what = kChangeBusy;
    if (r.direction != 2) what |= kChangePath;   // 뒤로/앞으로 버튼 상태
    // 같은 폴더이고 내용이 완전히 같으면 목록을 건드리지 않는다(깜빡임·스크롤·선택 유지).
    const bool same = !pathChanged && SameListing(*entries_, r.entries);
    if (!same) {
        status_ = FormatThousands(static_cast<int64_t>(r.entries.size())) + L"개 항목";
        entries_ = std::make_shared<const std::vector<FileEntry>>(std::move(r.entries));
        what |= kChangeEntries;
    } else {
        status_ = FormatThousands(static_cast<int64_t>(entries_->size())) + L"개 항목";
    }
    if (pathChanged) what |= kChangePath | kChangeEntries;
    Notify(what);
    Finish(true);
}

void DispatchDirResult(DirResult* result) {
    std::unique_ptr<DirResult> owned(result);
    if (auto tab = owned->tab.lock()) tab->OnResult(*owned);
}

}  // namespace ft
