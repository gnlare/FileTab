#include "FileView.h"
#include "core/GridNavigation.h"
#include "core/QuickFind.h"
#include <cwctype>

#include <algorithm>
#include <cstdlib>
#include <unordered_set>

#include "FsServices.h"
#include "SettingsService.h"

namespace ft {

namespace {
const wchar_t* kClassName = L"FileTabFileView";

int ViewHeight(ViewMode mode, int clientH, int headerH) { return mode == ViewMode::Details ? std::max(0, clientH - headerH) : clientH; }
}  // namespace

class FileDropTarget final : public IDropTarget {
public:
    explicit FileDropTarget(FileView* view) : view_(view) {}

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
        if (!data || !hasFiles_ || !view_) return S_OK;
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
            ScreenToClient(view_->hwnd(), &p);
            int itemIdx = view_->HitTest(p.x, p.y);
            view_->HandleDroppedFiles(paths, itemIdx, keyState);
            *effect = (keyState & MK_SHIFT) ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
        }
        hasFiles_ = false;
        return S_OK;
    }

private:
    DWORD Effect(DWORD keyState) const {
        if (!hasFiles_) return DROPEFFECT_NONE;
        return (keyState & MK_SHIFT) ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
    }

    FileView* view_;
    ULONG refs_ = 1;
    bool hasFiles_ = false;
};

bool FileView::RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &FileView::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    return RegisterClassExW(&wc) != 0;
}

FileView* FileView::Create(HWND parent, Host* host) {
    auto* view = new FileView();
    view->host_ = host;
    view->hwnd_ = CreateWindowExW(0, kClassName, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, 10,
                                  10, parent, nullptr, GetModuleHandleW(nullptr), view);
    if (!view->hwnd_) {
        delete view;
        return nullptr;
    }
    // 항목 경로 툴팁(마우스가 멈추면 전체 경로를 보여 준다)
    view->tip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT,
                                 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, view->hwnd_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (view->tip_) {
        TOOLINFOW ti{};
        ti.cbSize = sizeof ti;
        ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = view->hwnd_;
        ti.uId = 1;
        ti.lpszText = LPSTR_TEXTCALLBACKW;
        SendMessageW(view->tip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
        SendMessageW(view->tip_, TTM_SETMAXTIPWIDTH, 0, 700);
    }
    view->dropTarget_ = new FileDropTarget(view);
    RegisterDragDrop(view->hwnd_, view->dropTarget_);
    view->ComputeLayout();
    return view;
}

FileView::~FileView() {
    if (dropTarget_) {
        RevokeDragDrop(hwnd_);
        dropTarget_->Release();
        dropTarget_ = nullptr;
    }
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
    if (memDc_) {
        if (backOld_) SelectObject(memDc_, backOld_);
        DeleteDC(memDc_);
    }
    if (scratchDc_) DeleteDC(scratchDc_);
    if (backBmp_) DeleteObject(backBmp_);
}

LRESULT CALLBACK FileView::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    FileView* self;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<FileView*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<FileView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->HandleMessage(msg, wParam, lParam);
}

// ======================= 배치 / 스크롤 =======================

void FileView::ComputeLayout() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    cw_ = rc.right;
    fullH_ = rc.bottom;
    ch_ = std::max(0, fullH_ - (Finding() ? ui::S(36) : 0));
    m_.rowH = ui::S(30);
    m_.headerH = ui::S(26);
    m_.nameIcon = ui::S(20);
    m_.iconPx = mode_ == ViewMode::LargeIcons ? 128 : 64;   // 실제 화면 픽셀 기준(WPF 버전과 같은 규칙)
    const IconMargins& mg = SettingsService::Get().Margins(mode_);
    m_.tileW = m_.iconPx + ui::S(16 + 2 * mg.horizontal);
    m_.tileH = m_.iconPx + ui::S(32 + 2 * mg.vertical);
    cols_ = std::max(1, cw_ / std::max(1, m_.tileW));
    scrollY_ = std::clamp(scrollY_, 0, MaxScroll());
    UpdateScrollBar();
    if (tip_) {
        TOOLINFOW ti{};
        ti.cbSize = sizeof ti;
        ti.hwnd = hwnd_;
        ti.uId = 1;
        ti.rect = rc;
        SendMessageW(tip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&ti));
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

int FileView::ContentHeight() const {
    int n = ItemCount();
    if (mode_ == ViewMode::Details) return n * m_.rowH;
    return ((n + cols_ - 1) / cols_) * m_.tileH;
}

int FileView::MaxScroll() const { return std::max(0, ContentHeight() - ViewHeight(mode_, ch_, m_.headerH)); }

RECT FileView::ThumbRect() const {
    if (MaxScroll() <= 0) return {};
    int vh = ViewHeight(mode_, ch_, m_.headerH);
    int thH = std::min(vh, std::max(ui::S(24), static_cast<int>(static_cast<int64_t>(vh) * vh / std::max(1, ContentHeight()))));
    int trackH = std::max(1, vh - thH);
    int thY = (mode_ == ViewMode::Details ? m_.headerH : 0) + static_cast<int>(static_cast<int64_t>(trackH) * scrollY_ / MaxScroll());
    return {cw_ - ui::S(7), thY, cw_ - ui::S(2), thY + thH};
}

void FileView::UpdateScrollBar() {
    ShowScrollBar(hwnd_, SB_VERT, FALSE);
}

void FileView::SetScroll(int y) {
    int clamped = std::clamp(y, 0, MaxScroll());
    bool changed = clamped != scrollY_;
    scrollY_ = clamped;
    UpdateScrollBar();
    if (changed) InvalidateRect(hwnd_, nullptr, FALSE);
}

RECT FileView::ItemRect(int i) const {
    if (mode_ == ViewMode::Details) {
        int y = m_.headerH + i * m_.rowH - scrollY_;
        return {0, y, cw_, y + m_.rowH};
    }
    int x = (i % cols_) * m_.tileW;
    int y = (i / cols_) * m_.tileH - scrollY_;
    return {x, y, x + m_.tileW, y + m_.tileH};
}

void FileView::EnsureVisible(int i) {
    if (i < 0 || i >= ItemCount()) return;
    int unit = mode_ == ViewMode::Details ? m_.rowH : m_.tileH;
    int row = mode_ == ViewMode::Details ? i : i / cols_;
    int top = row * unit, bottom = top + unit;
    int viewH = ViewHeight(mode_, ch_, m_.headerH);
    if (top < scrollY_) SetScroll(top);
    else if (bottom > scrollY_ + viewH) SetScroll(bottom - viewH);
}

int FileView::HitTest(int x, int y) const {
    int n = ItemCount();
    if (n == 0 || x < 0 || y < 0 || y >= ch_) return -1;
    if (mode_ == ViewMode::Details) {
        if (y < m_.headerH || x >= cw_) return -1;
        int idx = (y - m_.headerH + scrollY_) / m_.rowH;
        return idx < n ? idx : -1;
    }
    if (x >= cols_ * m_.tileW) return -1;
    int idx = ((y + scrollY_) / m_.tileH) * cols_ + x / m_.tileW;
    return idx < n ? idx : -1;
}

void FileView::ColumnLayout(int& nameW, int& sizeW, int& modW) const {
    sizeW = ui::S(sizeColDip_);
    modW = ui::S(modColDip_);
    nameW = std::max(ui::S(kNameMinDip), cw_ - sizeW - modW);
}

int FileView::HeaderDivider(int x) const {
    int nameW, sizeW, modW;
    ColumnLayout(nameW, sizeW, modW);
    int grab = ui::S(4);
    if (std::abs(x - nameW) <= grab) return 1;
    if (std::abs(x - (nameW + sizeW)) <= grab) return 2;
    return 0;
}

int FileView::HeaderColumn(int x) const {
    int nameW, sizeW, modW;
    ColumnLayout(nameW, sizeW, modW);
    if (x < nameW) return 0;
    if (x < nameW + sizeW) return 1;
    return 2;
}

// ======================= 선택 =======================

void FileView::ClearSelection() {
    std::fill(sel_.begin(), sel_.end(), static_cast<char>(0));
    selCount_ = 0;
}

void FileView::SelectOnly(int i) {
    ClearSelection();
    if (i >= 0 && i < ItemCount()) {
        sel_[static_cast<size_t>(i)] = 1;
        selCount_ = 1;
    }
}

void FileView::SelectRange(int a, int b) {
    if (a > b) std::swap(a, b);
    a = std::max(a, 0);
    b = std::min(b, ItemCount() - 1);
    for (int k = a; k <= b; ++k)
        if (!sel_[static_cast<size_t>(k)]) {
            sel_[static_cast<size_t>(k)] = 1;
            ++selCount_;
        }
}

void FileView::ToggleSelect(int i) {
    if (i < 0 || i >= ItemCount()) return;
    char& s = sel_[static_cast<size_t>(i)];
    s = s ? 0 : 1;
    selCount_ += s ? 1 : -1;
}

void FileView::NotifySelection() {
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (host_) host_->FvSelectionChanged();
}

void FileView::MoveFocus(int target, bool extend, bool ctrlOnly) {
    int n = ItemCount();
    if (n == 0) return;
    target = std::clamp(target, 0, n - 1);
    if (extend) {
        if (anchor_ < 0) anchor_ = focus_ >= 0 ? focus_ : target;
        ClearSelection();
        SelectRange(anchor_, target);
    } else if (!ctrlOnly) {
        SelectOnly(target);
        anchor_ = target;
    }
    focus_ = target;
    EnsureVisible(target);
    NotifySelection();
}

std::vector<FileEntry> FileView::SelectedEntries() const {
    std::vector<FileEntry> result;
    const size_t count = std::min(items_ ? items_->size() : 0, sel_.size());
    for (size_t i = 0; i < count; ++i)
        if (sel_[i]) result.push_back((*items_)[i]);
    return result;
}

std::wstring FileView::FirstSelectedName() const {
    const size_t count = std::min(items_ ? items_->size() : 0, sel_.size());
    for (size_t i = 0; i < count; ++i)
        if (sel_[i]) return (*items_)[i].name;
    return std::wstring();
}

std::wstring FileView::FullPath(const FileEntry& e) const { return path::Combine(dir_, e.name); }

void FileView::HandleDroppedFiles(const std::vector<std::wstring>& paths, int targetItemIdx, DWORD keyState) {
    if (paths.empty() || !host_) return;
    std::wstring targetDir;
    if (targetItemIdx >= 0 && items_ && static_cast<size_t>(targetItemIdx) < items_->size()) {
        const auto& entry = (*items_)[static_cast<size_t>(targetItemIdx)];
        if (entry.IsDir()) {
            targetDir = FullPath(entry);
        }
    }
    if (targetDir.empty()) {
        targetDir = tab_ ? tab_->Path() : L"";
    }
    if (targetDir.empty()) return;
    bool isMove = (keyState & MK_SHIFT) != 0;
    host_->FvDropFiles(paths, targetDir, isMove);
}

std::vector<std::wstring> FileView::SelectedPaths() const {
    std::vector<std::wstring> result;
    const size_t count = std::min(items_ ? items_->size() : 0, sel_.size());
    for (size_t i = 0; i < count; ++i)
        if (sel_[i]) result.push_back(FullPath((*items_)[i]));
    return result;
}

void FileView::RestoreSelection(const std::vector<std::wstring>& names, const std::wstring& focusName) {
    int n = ItemCount();
    sel_.assign(static_cast<size_t>(n), 0);
    selCount_ = 0;
    anchor_ = focus_ = -1;
    if (names.empty() && focusName.empty()) return;
    std::unordered_set<std::wstring> wanted(names.begin(), names.end());
    for (int i = 0; i < n; ++i) {
        const std::wstring& name = (*items_)[static_cast<size_t>(i)].name;
        if (wanted.count(name)) {
            sel_[static_cast<size_t>(i)] = 1;
            ++selCount_;
        }
        if (!focusName.empty() && name == focusName) focus_ = i;
    }
    anchor_ = focus_;
}

void FileView::SelectOnlyByName(const std::wstring& name) {
    for (int i = 0; i < ItemCount(); ++i)
        if ((*items_)[static_cast<size_t>(i)].name == name) {
            SelectOnly(i);
            anchor_ = focus_ = i;
            EnsureVisible(i);
            NotifySelection();
            return;
        }
}

bool FileView::HasFocus() const { return GetFocus() == hwnd_; }
void FileView::Focus() { SetFocus(hwnd_); }
void FileView::RepaintNow() { InvalidateRect(hwnd_, nullptr, FALSE); }

// ======================= 탭 연결 =======================

void FileView::GetViewState(FolderViewState& out) const {
    out.scrollY = scrollY_;
    out.selection.clear();
    const size_t count = std::min(items_ ? items_->size() : 0, sel_.size());
    for (size_t i = 0; i < count; ++i)
        if (sel_[i]) out.selection.push_back((*items_)[i].name);
    out.focus = (focus_ >= 0 && focus_ < ItemCount()) ? (*items_)[static_cast<size_t>(focus_)].name : std::wstring();
}

void FileView::SaveStateToTab() {
    if (!tab_) return;
    tab_->savedSelection.clear();
    const size_t count = std::min(items_ ? items_->size() : 0, sel_.size());
    for (size_t i = 0; i < count; ++i)
        if (sel_[i]) tab_->savedSelection.push_back((*items_)[i].name);
    tab_->savedFocus = (focus_ >= 0 && focus_ < ItemCount()) ? (*items_)[static_cast<size_t>(focus_)].name : std::wstring();
    tab_->savedScroll = scrollY_;

    FolderViewState vs;
    vs.scrollY = scrollY_;
    vs.selection = tab_->savedSelection;
    vs.focus = tab_->savedFocus;
    tab_->SaveViewState(tab_->Path(), vs);
}

void FileView::SetTab(std::shared_ptr<FolderTab> tab) {
    EndFind(true);
    if (tab_) SaveStateToTab();
    tab_ = std::move(tab);
    typeBuf_.clear();
    if (!tab_) {
        items_.reset();
        dir_.clear();
        sel_.clear();
        selCount_ = 0;
        focus_ = anchor_ = -1;
        scrollY_ = 0;
        ComputeLayout();
        NotifySelection();
        return;
    }
    items_ = tab_->Entries();
    dir_ = tab_->Path();
    mode_ = tab_->Mode();
    RestoreSelection(tab_->savedSelection, tab_->savedFocus);
    scrollY_ = tab_->savedScroll;
    ComputeLayout();
    NotifySelection();
}

void FileView::OnTabChanged(unsigned what) {
    if (!tab_) return;
    if (what & (kChangeEntries | kChangeMode | kChangeSort)) EndFind(false);
    if (what & kChangeMode) {
        mode_ = tab_->Mode();
        ComputeLayout();
        if (focus_ >= 0) EnsureVisible(focus_);
    }
    if (what & kChangeEntries) {
        const int unit = mode_ == ViewMode::Details ? m_.rowH : m_.tileH;
        auto rowOf = [&](int idx) { return mode_ == ViewMode::Details ? idx : idx / cols_; };
        // 바뀌기 전 상태를 이름으로 기억한다.
        std::vector<std::wstring> names;
        const size_t oldCount = std::min(items_ ? items_->size() : 0, sel_.size());
        for (size_t i = 0; i < oldCount; ++i)
            if (sel_[i]) names.push_back((*items_)[i].name);
        std::wstring focusName = (focus_ >= 0 && focus_ < ItemCount()) ? (*items_)[static_cast<size_t>(focus_)].name : std::wstring();
        const int oldTop = scrollY_ / unit;
        const int frac = scrollY_ % unit;
        std::wstring topName;
        int topIdx = mode_ == ViewMode::Details ? oldTop : oldTop * cols_;
        if (topIdx >= 0 && topIdx < ItemCount()) topName = (*items_)[static_cast<size_t>(topIdx)].name;

        const bool pathChanged = !EqualsNoCase(dir_, tab_->Path());
        items_ = tab_->Entries();
        dir_ = tab_->Path();
        bool needScrollToFocus = false;
        if (pathChanged) {
            if (tab_->HasPendingRestore()) {
                auto restore = tab_->TakePendingRestore();
                RestoreSelection(restore.selection, restore.focus);
                if (restore.scrollY >= 0) {
                    scrollY_ = restore.scrollY;
                } else {
                    scrollY_ = 0;
                    needScrollToFocus = (focus_ >= 0);
                }
            } else {
                sel_.assign(static_cast<size_t>(ItemCount()), 0);
                selCount_ = 0;
                anchor_ = focus_ = -1;
                scrollY_ = 0;
            }
        } else {
            RestoreSelection(names, focusName);
            // 같은 폴더: 맨 위에 보이던 항목을 따라가 스크롤 위치를 유지한다.
            if (!topName.empty()) {
                for (int i = 0; i < ItemCount(); ++i)
                    if ((*items_)[static_cast<size_t>(i)].name == topName) {
                        scrollY_ = rowOf(i) * unit + frac;
                        break;
                    }
            }
        }
        typeBuf_.clear();
        ComputeLayout();
        if (needScrollToFocus && focus_ >= 0) EnsureVisible(focus_);
        NotifySelection();
        return;
    }
    if (what & kChangeSort) InvalidateRect(hwnd_, nullptr, FALSE);
    if (what & kChangeMode) NotifySelection();
}

void FileView::ApplyMetrics() { ComputeLayout(); }

// ======================= 그리기 =======================

void FileView::Paint() {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(hwnd_, &ps);
    if (cw_ > 0 && fullH_ > 0) {
        if (!memDc_) {
            memDc_ = CreateCompatibleDC(wdc);
            scratchDc_ = CreateCompatibleDC(wdc);
        }
        if (!backBmp_ || backW_ != cw_ || backH_ != fullH_) {
            if (backBmp_) {
                SelectObject(memDc_, backOld_);
                DeleteObject(backBmp_);
            }
            backBmp_ = CreateCompatibleBitmap(wdc, cw_, fullH_);
            backW_ = cw_;
            backH_ = fullH_;
            backOld_ = static_cast<HBITMAP>(SelectObject(memDc_, backBmp_));
        }
        const uint64_t frame = epoch_->fetch_add(1) + 1;
        RECT all{0, 0, cw_, fullH_};
        ui::FillRectColor(memDc_, all, ui::kPanelBg);
        SetBkMode(memDc_, TRANSPARENT);
        int clip = SaveDC(memDc_);
        IntersectClipRect(memDc_, 0, 0, cw_, ch_);
        if (mode_ == ViewMode::Details) PaintDetails(memDc_, frame);
        else PaintTiles(memDc_, frame);
        if (MaxScroll() > 0) {
            RECT tr = ThumbRect();
            RECT track{tr.left, mode_ == ViewMode::Details ? m_.headerH : 0, tr.right, ch_};
            ui::FillRectColor(memDc_, track, ui::kStripBg);
            COLORREF thumbColor = draggingThumb_ ? ui::kText : (ui::IsDarkMode() ? RGB(0x60, 0x60, 0x65) : RGB(0xB8, 0xC0, 0xCC));
            ui::FillRectColor(memDc_, tr, thumbColor);
        }
        RestoreDC(memDc_, clip);
        if (Finding()) PaintFindBar(memDc_);
        BitBlt(wdc, 0, 0, cw_, fullH_, memDc_, 0, 0, SRCCOPY);
    }
    EndPaint(hwnd_, &ps);
}

void FileView::DrawSortArrow(HDC dc, int cx, int cy, bool descending) {
    const int s = ui::S(4);
    POINT pts[3];
    if (!descending) {
        pts[0] = {cx - s, cy + s / 2};
        pts[1] = {cx + s, cy + s / 2};
        pts[2] = {cx, cy - s / 2};
    } else {
        pts[0] = {cx - s, cy - s / 2};
        pts[1] = {cx + s, cy - s / 2};
        pts[2] = {cx, cy + s / 2};
    }
    HBRUSH brush = CreateSolidBrush(ui::kTextDim);
    HPEN pen = CreatePen(PS_SOLID, 1, ui::kTextDim);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polygon(dc, pts, 3);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void FileView::PaintDetails(HDC dc, uint64_t frame) {
    const int n = ItemCount();
    int nameW, sizeW, modW;
    ColumnLayout(nameW, sizeW, modW);
    const bool focused = HasFocus();
    const int viewH = ViewHeight(mode_, ch_, m_.headerH);
    ImageService& images = ImageService::Get();
    const UINT textFlags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;

    if (n > 0) {
        const int first = std::max(0, scrollY_ / m_.rowH);
        const int last = std::min(n - 1, (scrollY_ + viewH - 1) / m_.rowH);
        for (int i = first; i <= last; ++i) {
            const FileEntry& e = (*items_)[static_cast<size_t>(i)];
            const int y = m_.headerH + i * m_.rowH - scrollY_;
            RECT row{0, y, cw_, y + m_.rowH};
            const bool selected = static_cast<size_t>(i) < sel_.size() && sel_[static_cast<size_t>(i)] != 0;
            COLORREF bg = selected ? (focused ? ui::kSelectFill : ui::kSelectFillInactive) : ((i & 1) ? ui::kRowAlt : ui::kPanelBg);
            ui::FillRectColor(dc, row, bg);

            ImagePtr img = images.ForEntry(e, dir_, m_.nameIcon, false, epoch_, frame);
            RECT iconCell{ui::S(4), y, ui::S(4) + m_.nameIcon, y + m_.rowH};
            if (img) ImageService::Draw(dc, scratchDc_, *img, iconCell);

            RECT nameRc{iconCell.right + ui::S(8), y, nameW - ui::S(4), y + m_.rowH};
            DrawFindName(dc, i, e.name, nameRc, textFlags);
            if (!e.IsDir()) {
                RECT sizeRc{nameW + ui::S(4), y, nameW + sizeW - ui::S(4), y + m_.rowH};
                ui::DrawTextIn(dc, FormatSize(static_cast<int64_t>(e.size)), sizeRc, textFlags, ui::kText);
            }
            RECT modRc{nameW + sizeW + ui::S(4), y, cw_ - ui::S(4), y + m_.rowH};
            ui::DrawTextIn(dc, FormatModified(e.modified), modRc, textFlags, ui::kText);

            if (focused && i == focus_) {
                RECT fr = row;
                InflateRect(&fr, -1, -1);
                ui::FrameRectColor(dc, fr, ui::kSelectBorder);
            }
        }
    }

    // 헤더(스크롤 영역 위에 덮어 그린다)
    RECT header{0, 0, cw_, m_.headerH};
    ui::FillRectColor(dc, header, ui::kHeaderBg);
    RECT line{0, m_.headerH - 1, cw_, m_.headerH};
    ui::FillRectColor(dc, line, ui::kFieldBorder);
    const wchar_t* labels[3] = {L"이름", L"크기", L"수정한 날짜"};
    const int xs[4] = {0, nameW, nameW + sizeW, cw_};
    const SortKey sortKey = tab_ ? tab_->SortColumn() : SortKey::Name;
    const bool desc = tab_ ? tab_->SortDescending() : false;
    for (int c = 0; c < 3; ++c) {
        RECT tr{xs[c] + ui::S(8), 0, xs[c + 1] - ui::S(18), m_.headerH - 1};
        ui::DrawTextIn(dc, labels[c], tr, textFlags, ui::kText);
        if (static_cast<int>(sortKey) == c) DrawSortArrow(dc, xs[c + 1] - ui::S(10), m_.headerH / 2, desc);
        if (c > 0) {
            RECT sep{xs[c], ui::S(4), xs[c] + 1, m_.headerH - ui::S(5)};
            ui::FillRectColor(dc, sep, ui::kFieldBorder);
        }
    }
}

void FileView::PaintTiles(HDC dc, uint64_t frame) {
    const int n = ItemCount();
    if (n == 0) return;
    const bool focused = HasFocus();
    ImageService& images = ImageService::Get();
    const int textH = ui::TextHeight(dc);
    const int gap = ui::S(4);
    const int nominal = ui::S(mode_ == ViewMode::LargeIcons ? 128 : 64);   // WPF 버전: 이름 영역 너비 = 아이콘 크기(DIP)
    const int textW = std::min(nominal, m_.tileW - ui::S(4));
    const int firstRow = std::max(0, scrollY_ / m_.tileH);
    const int lastRow = (scrollY_ + ch_ - 1) / m_.tileH;
    for (int row = firstRow; row <= lastRow; ++row) {
        for (int col = 0; col < cols_; ++col) {
            const int i = row * cols_ + col;
            if (i >= n) break;
            const FileEntry& e = (*items_)[static_cast<size_t>(i)];
            const int x0 = col * m_.tileW;
            const int y0 = row * m_.tileH - scrollY_;
            RECT tile{x0, y0, x0 + m_.tileW, y0 + m_.tileH};
            if (static_cast<size_t>(i) < sel_.size() && sel_[static_cast<size_t>(i)]) {
                RECT sr = tile;
                InflateRect(&sr, -ui::S(2), -ui::S(2));
                ui::FillRectColor(dc, sr, focused ? ui::kSelectFill : ui::kSelectFillInactive);
                ui::FrameRectColor(dc, sr, ui::kSelectBorder);
            }
            const int contentH = m_.iconPx + gap + textH;
            const int top = y0 + (m_.tileH - contentH) / 2;
            RECT iconCell{x0, top, x0 + m_.tileW, top + m_.iconPx};
            ImagePtr img = images.ForEntry(e, dir_, m_.iconPx, true, epoch_, frame);
            if (img) ImageService::Draw(dc, scratchDc_, *img, iconCell);

            RECT textRc{x0 + (m_.tileW - textW) / 2, top + m_.iconPx + gap, x0 + (m_.tileW + textW) / 2, top + m_.iconPx + gap + textH};
            // 이름이 영역에 들어가면 가운데, 넘치면 왼쪽 정렬로 말줄임(WPF 버전과 같은 규칙)
            UINT align = ui::TextWidth(dc, e.name) <= textW ? DT_CENTER : DT_LEFT;
            DrawFindName(dc, i, e.name, textRc, align | DT_SINGLELINE | DT_END_ELLIPSIS);

            if (focused && i == focus_) {
                RECT fr = tile;
                InflateRect(&fr, -ui::S(3), -ui::S(3));
                ui::FrameRectColor(dc, fr, ui::kSelectBorder);
            }
        }
    }
}

// ======================= 입력 =======================

void FileView::OnLButtonDown(int x, int y, WPARAM keys) {
    SetFocus(hwnd_);
    if (host_) host_->FvActivate();
    if (y >= ch_) return;
    if (!Finding()) EndFind(false);

    if (MaxScroll() > 0) {
        RECT tr = ThumbRect();
        if (ui::PtIn(tr, x, y)) {
            draggingThumb_ = true;
            dragStartY_ = y;
            dragStartScroll_ = scrollY_;
            SetCapture(hwnd_);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        RECT track{tr.left, mode_ == ViewMode::Details ? m_.headerH : 0, tr.right, ch_};
        if (ui::PtIn(track, x, y)) {
            SetScroll(scrollY_ + (y < tr.top ? -1 : 1) * ViewHeight(mode_, ch_, m_.headerH));
            return;
        }
    }

    if (mode_ == ViewMode::Details && y < m_.headerH) {
        if (int div = HeaderDivider(x)) {
            resizingCol_ = div;
            resizeStartX_ = x;
            resizeStartDip_ = div == 1 ? sizeColDip_ : modColDip_;
            resized_ = false;
        } else {
            headerDown_ = HeaderColumn(x);
        }
        SetCapture(hwnd_);
        return;
    }

    const bool ctrl = (keys & MK_CONTROL) != 0;
    const bool shift = (keys & MK_SHIFT) != 0;
    const int idx = HitTest(x, y);
    collapseTo_ = -1;
    if (idx < 0) {
        if (!ctrl && !shift) {
            ClearSelection();
            NotifySelection();
        }
        return;
    }
    if (shift) {
        if (anchor_ < 0) anchor_ = idx;
        if (!ctrl) ClearSelection();
        SelectRange(anchor_, idx);
    } else if (ctrl) {
        ToggleSelect(idx);
        anchor_ = idx;
    } else if (sel_[static_cast<size_t>(idx)] && selCount_ > 1) {
        collapseTo_ = idx;   // 끌어서 여러 항목을 옮길 수 있게, 버튼을 뗄 때 이 항목만 선택
    } else {
        SelectOnly(idx);
        anchor_ = idx;
    }
    focus_ = idx;
    dragCandidate_ = true;
    downPt_ = {x, y};
    SetCapture(hwnd_);
    NotifySelection();
}

void FileView::OnMouseMove(int x, int y, WPARAM keys) {
    if (draggingThumb_) {
        int vh = ViewHeight(mode_, ch_, m_.headerH);
        int thH = std::min(vh, std::max(ui::S(24), static_cast<int>(static_cast<int64_t>(vh) * vh / std::max(1, ContentHeight()))));
        int trackH = std::max(1, vh - thH);
        int dy = y - dragStartY_;
        SetScroll(dragStartScroll_ + static_cast<int>(static_cast<int64_t>(dy) * MaxScroll() / trackH));
        return;
    }
    if (resizingCol_) {
        int px = ui::S(resizeStartDip_) + (resizeStartX_ - x);
        int dip = std::clamp(MulDiv(px, 96, static_cast<int>(ui::dpi)), 40, 600);
        (resizingCol_ == 1 ? sizeColDip_ : modColDip_) = dip;
        resized_ = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (dragCandidate_ && (keys & MK_LBUTTON)) {
        if (std::abs(x - downPt_.x) >= GetSystemMetrics(SM_CXDRAG) || std::abs(y - downPt_.y) >= GetSystemMetrics(SM_CYDRAG)) {
            dragCandidate_ = false;
            collapseTo_ = -1;
            ReleaseCapture();
            std::vector<std::wstring> paths = SelectedPaths();
            if (!paths.empty()) StartFileDrag(paths);
        }
        return;
    }
    int hot = HitTest(x, y);
    if (hot != hot_) {
        hot_ = hot;
        if (tip_) SendMessageW(tip_, TTM_POP, 0, 0);
    }
}

void FileView::OnLButtonUp(int x, int y) {
    if (GetCapture() == hwnd_) ReleaseCapture();
    if (draggingThumb_) {
        draggingThumb_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (resizingCol_) {
        resizingCol_ = 0;
        return;
    }
    if (headerDown_ >= 0) {
        int col = headerDown_;
        headerDown_ = -1;
        if (y < m_.headerH && HeaderColumn(x) == col && host_) host_->FvSortRequested(static_cast<SortKey>(col));
        return;
    }
    if (collapseTo_ >= 0) {
        SelectOnly(collapseTo_);
        anchor_ = collapseTo_;
        collapseTo_ = -1;
        NotifySelection();
    }
    dragCandidate_ = false;
}

void FileView::OnKeyDown(WPARAM key) {
    if (HandleFindKey(key)) return;
    if (!Finding() && !(key >= '0' && key <= '9') && !(key >= 'A' && key <= 'Z') && !(key >= VK_NUMPAD0 && key <= VK_NUMPAD9) && key != VK_SHIFT) EndFind(false);
    const int n = ItemCount();
    if (n == 0) return;
    const bool ctrl = GetKeyState(VK_CONTROL) < 0;
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    const bool tiles = mode_ != ViewMode::Details;
    const int cur = focus_;
    const int page = tiles ? std::max(1, ch_ / m_.tileH) * cols_ : std::max(1, (ch_ - m_.headerH) / m_.rowH);
    int target = -1;
    switch (key) {
        case VK_UP: target = cur < 0 ? 0 : cur - (tiles ? cols_ : 1); break;
        case VK_DOWN:
            target = cur < 0 ? 0 : cur + (tiles ? cols_ : 1);
            if (tiles && cur >= 0 && target >= n) target = (cur / cols_ < (n - 1) / cols_) ? n - 1 : cur;
            break;
        case VK_LEFT: if (tiles) target = cur < 0 ? 0 : cur - 1; break;
        case VK_RIGHT: if (tiles) target = cur < 0 ? 0 : cur + 1; break;
        case VK_HOME: target = 0; break;
        case VK_END: target = n - 1; break;
        case VK_PRIOR: target = tiles ? GridPageTarget(n, cols_, cur, page / cols_, false) : std::max(0, cur - page); break;
        case VK_NEXT: target = tiles ? GridPageTarget(n, cols_, cur, page / cols_, true) : (cur < 0 ? 0 : cur + page); break;
        case 'A':
            if (ctrl) {
                std::fill(sel_.begin(), sel_.end(), static_cast<char>(1));
                selCount_ = n;
                NotifySelection();
            }
            return;
        case VK_SPACE:
            if (ctrl && cur >= 0) {
                ToggleSelect(cur);
                anchor_ = cur;
                NotifySelection();
            }
            return;
        default: return;
    }
    if (target >= 0) MoveFocus(target, shift, ctrl);
}

void FileView::DrawFindName(HDC dc, int index, const std::wstring& name, RECT rc, UINT flags) {
    if (ui::RcW(rc) <= 0 || ui::RcH(rc) <= 0) return;
    const bool match = Finding() && std::binary_search(findMatches_.begin(), findMatches_.end(), index);
    if (!match) {
        ui::DrawTextIn(dc, name, rc, flags, ui::kText);
        return;
    }
    // Ask DrawText for the actual ellipsized text, then highlight only visible matching characters.
    const int saved = SaveDC(dc);
    if (!saved) { ui::DrawTextIn(dc, name, rc, flags, ui::kText); return; }
    IntersectClipRect(dc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(dc, ui::font);
    std::vector<wchar_t> buffer(name.begin(), name.end());
    buffer.resize(name.size() + 5, L'\0');
    RECT actual = rc;
    SetTextColor(dc, ui::kText);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, buffer.data(), -1, &actual, flags | DT_NOPREFIX | DT_MODIFYSTRING);
    const std::wstring displayed(buffer.data());
    size_t visible = 0;
    while (visible < name.size() && visible < displayed.size() && name[visible] == displayed[visible]) ++visible;
    const int width = ui::TextWidth(dc, displayed);
    const int x = rc.left + ((flags & DT_CENTER) ? std::max(0, (ui::RcW(rc) - width) / 2) : 0);
    const int height = std::min(ui::RcH(rc), ui::TextHeight(dc));
    const int y = rc.top + ((flags & DT_VCENTER) ? (ui::RcH(rc) - height) / 2 : 0);
    for (const auto& range : FindRanges(name, typeBuf_)) {
        if (range.first >= visible) break;
        const size_t end = std::min(visible, range.first + range.second);
        RECT mark{x + ui::TextWidth(dc, name.substr(0, range.first)), y,
                  x + ui::TextWidth(dc, name.substr(0, end)), y + height};
        const int clip = SaveDC(dc);
        if (!clip) continue;
        IntersectClipRect(dc, mark.left, mark.top, mark.right, mark.bottom);
        ui::FillRectColor(dc, mark, ui::IsDarkMode() ? RGB(119, 87, 16) : RGB(255, 224, 102));
        ui::DrawTextIn(dc, name, rc, flags, ui::IsDarkMode() ? RGB(255, 255, 255) : RGB(25, 25, 25));
        RestoreDC(dc, clip);
    }
    RestoreDC(dc, saved);
}

void FileView::PaintFindBar(HDC dc) {
    RECT bar{0, ch_, cw_, fullH_};
    ui::FillRectColor(dc, bar, ui::kStripBg);
    ui::FillRectColor(dc, RECT{0, ch_, cw_, ch_ + 1}, ui::kFieldBorder);
    RECT label{ui::S(8), ch_, ui::S(50), fullH_};
    ui::DrawTextIn(dc, L"찾기", label, DT_LEFT | DT_VCENTER | DT_SINGLELINE, ui::kText);
    RECT field{ui::S(52), ch_ + ui::S(5), cw_ - ui::S(8), fullH_ - ui::S(5)};
    if (ui::RcW(field) <= ui::S(12) || ui::RcH(field) <= 0) return;
    ui::FillRectColor(dc, field, ui::kFieldBg);
    ui::FrameRectColor(dc, field, ui::kFieldBorder);
    RECT inner = field;
    InflateRect(&inner, -ui::S(6), 0);

    // 순서값 / 찾은 아이템 개수 계산
    int order = 0;
    const int total = static_cast<int>(findMatches_.size());
    if (total > 0 && focus_ >= 0) {
        for (size_t i = 0; i < findMatches_.size(); ++i) {
            if (findMatches_[i] == focus_) {
                order = static_cast<int>(i + 1);
                break;
            }
        }
    }
    const std::wstring countStr = std::to_wstring(order) + L" / " + std::to_wstring(total);

    const int countW = ui::TextWidth(dc, countStr, ui::font);
    RECT countRc = inner;
    countRc.left = std::max(inner.left, inner.right - countW - ui::S(4));

    RECT queryRc = inner;
    queryRc.right = std::max(queryRc.left, countRc.left - ui::S(8));

    ui::DrawTextIn(dc, typeBuf_, queryRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, ui::kText);

    const COLORREF countColor = (total == 0 && !typeBuf_.empty()) ? RGB(220, 53, 69) : ui::kTextDim;
    ui::DrawTextIn(dc, countStr, countRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, countColor, ui::font);
}

void FileView::UpdateFind() {
    findMatches_.clear();
    if (Finding() && !typeBuf_.empty()) {
        for (int i = 0; i < ItemCount(); ++i)
            if (FindContains((*items_)[static_cast<size_t>(i)].name, typeBuf_)) findMatches_.push_back(i);
    }
    ComputeLayout();
    if (Finding() && !typeBuf_.empty()) {
        const int refFocus = focus_ >= 0 ? focus_ : (findSavedFocus_ >= 0 ? findSavedFocus_ : -1);
        const int target = FindClosestBelow(findMatches_, refFocus);
        if (target >= 0) MoveFocus(target, false, false);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FileView::EndFind(bool restore) {
    if (typeBuf_.empty() && !Finding()) return;
    bool wasFinding = Finding();
    findActive_ = false;
    typeBuf_.clear();
    findMatches_.clear();
    firstCharTick_ = 0;
    if (restore && wasFinding && findSavedSelection_.size() == sel_.size()) {
        sel_ = std::move(findSavedSelection_);
        selCount_ = findSavedCount_;
        focus_ = findSavedFocus_;
        anchor_ = findSavedAnchor_;
        scrollY_ = findSavedScroll_;
    }
    findSavedSelection_.clear();
    if (wasFinding) {
        ComputeLayout();
        NotifySelection();
    }
}

bool FileView::HandleFindKey(WPARAM key) {
    if (!HasFocus() || (typeBuf_.empty() && !Finding())) return false;
    if (GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0) {
        if (!Finding()) EndFind(false);
        return false;
    }
    if (key == VK_ESCAPE) { EndFind(true); return true; }
    if (Finding() && key == VK_TAB) {
        int target = FindNextMatch(findMatches_, focus_, GetKeyState(VK_SHIFT) < 0);
        if (target >= 0) MoveFocus(target, false, false);
        return true;
    }
    if (Finding() && key == VK_HOME) {
        int target = FindFirstMatch(findMatches_);
        if (target >= 0) MoveFocus(target, false, false);
        return true;
    }
    if (Finding() && key == VK_END) {
        if (!findMatches_.empty()) MoveFocus(findMatches_.back(), false, false);
        return true;
    }
    if (Finding() && key == VK_BACK) {
        if (!typeBuf_.empty()) {
            typeBuf_.pop_back();
            UpdateFind();
        }
        return true;
    }
    return false;
}

void FileView::OnChar(wchar_t ch) {
    if (GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0 || !tab_) return;
    if (!std::iswalnum(ch) && !(ch >= 0xAC00 && ch <= 0xD7A3) && !(ch >= 0x3131 && ch <= 0x318E) && !(ch >= 0x1100 && ch <= 0x11FF) && !(Finding() && ch >= 32 && ch != 127)) return;

    DWORD now = GetTickCount();

    if (Finding()) {
        // 이미 찾기 상태에 진입한 경우: 글자 추가 후 검색 갱신
        if (typeBuf_.size() >= 260) return;
        typeBuf_ += ch;
        UpdateFind();
        return;
    }

    // 아직 찾기 상태가 아닌 경우:
    // 첫 키 입력 후 2초가 지났거나 버퍼가 비어있으면 새로 첫 키로 시작
    if (typeBuf_.empty() || (now - firstCharTick_ > 2000)) {
        findSavedSelection_ = sel_;
        findSavedCount_ = selCount_;
        findSavedFocus_ = focus_;
        findSavedAnchor_ = anchor_;
        findSavedScroll_ = scrollY_;
        typeBuf_.clear();
        firstCharTick_ = now;
    }

    if (typeBuf_.size() >= 260) return;
    typeBuf_ += ch;

    if (typeBuf_.size() >= 2) {
        // 첫 키 입력 후 2초 이내에 2글자 이상 입력됨 -> 찾기 상태 진입
        findActive_ = true;
        UpdateFind();
    } else {
        // 키를 한 글자만 입력한 상태:
        // 해당 키로 시작하는 현재 아이템의 아래쪽으로 가장 가까운 아이템으로 이동
        int target = FindSingleKey(ItemCount(), focus_, typeBuf_, [&](int i) -> const std::wstring& {
            return (*items_)[static_cast<size_t>(i)].name;
        });
        if (target >= 0) MoveFocus(target, false, false);
    }
}

void FileView::OnWheel(int delta) {
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    int viewH = ViewHeight(mode_, ch_, m_.headerH);
    int amount;
    if (lines == WHEEL_PAGESCROLL) amount = viewH;
    else if (mode_ == ViewMode::Details) amount = m_.rowH * static_cast<int>(lines);
    else amount = std::max(1, m_.tileH * static_cast<int>(lines) / 3);   // 줄 수 3 = 아이콘 한 줄
    SetScroll(scrollY_ - delta * amount / WHEEL_DELTA);
}

void FileView::UpdateTooltipText(NMTTDISPINFOW* info) {
    tipText_ = L"";
    info->lpszText = const_cast<LPWSTR>(L"");
}

LRESULT FileView::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: Paint(); return 0;
        case WM_SIZE: ComputeLayout(); return 0;
        case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_SETFOCUS:
            if (host_) host_->FvActivate();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_KILLFOCUS: EndFind(false); InvalidateRect(hwnd_, nullptr, FALSE); return 0;
        case WM_VSCROLL: {
            SCROLLINFO si{};
            si.cbSize = sizeof si;
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd_, SB_VERT, &si);
            const int line = mode_ == ViewMode::Details ? m_.rowH : std::max(1, m_.tileH / 3);
            const int viewH = ViewHeight(mode_, ch_, m_.headerH);
            int pos = scrollY_;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: pos -= line; break;
                case SB_LINEDOWN: pos += line; break;
                case SB_PAGEUP: pos -= viewH; break;
                case SB_PAGEDOWN: pos += viewH; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: pos = si.nTrackPos; break;
                case SB_TOP: pos = 0; break;
                case SB_BOTTOM: pos = MaxScroll(); break;
                default: break;
            }
            SetScroll(pos);
            return 0;
        }
        case WM_MOUSEWHEEL: OnWheel(GET_WHEEL_DELTA_WPARAM(wParam)); return 0;
        case WM_LBUTTONDOWN: OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam); return 0;
        case WM_LBUTTONDBLCLK: {
            int idx = HitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (idx >= 0) {
                if (!sel_[static_cast<size_t>(idx)]) {
                    SelectOnly(idx);
                    focus_ = anchor_ = idx;
                    NotifySelection();
                }
                if (host_) host_->FvOpen();
            } else if (mode_ == ViewMode::Details && GET_Y_LPARAM(lParam) < m_.headerH) {
                OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);   // 빠른 연속 클릭도 정렬/크기 조절로 처리
            }
            return 0;
        }
        case WM_MOUSEMOVE: OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam); return 0;
        case WM_LBUTTONUP: OnLButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lParam) != hwnd_) {
                resizingCol_ = 0;
                headerDown_ = -1;
                draggingThumb_ = false;
            }
            return 0;
        case WM_RBUTTONDOWN: {
            if (!Finding()) EndFind(false);
            if (GET_Y_LPARAM(lParam) >= ch_) return 0;
            SetFocus(hwnd_);
            if (host_) host_->FvActivate();
            int idx = HitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (idx >= 0 && !sel_[static_cast<size_t>(idx)]) {
                SelectOnly(idx);
                focus_ = anchor_ = idx;
                NotifySelection();
            }
            return 0;
        }
        case WM_CONTEXTMENU: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (lParam == -1) {   // 키보드(Shift+F10, 메뉴 키)
                RECT r = focus_ >= 0 ? ItemRect(focus_) : RECT{0, 0, 0, 0};
                pt = {r.left + ui::S(16), r.top + ui::S(16)};
                ClientToScreen(hwnd_, &pt);
            }
            if (host_) host_->FvContextMenu(pt);
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && mode_ == ViewMode::Details) {
                POINT p;
                GetCursorPos(&p);
                ScreenToClient(hwnd_, &p);
                if (resizingCol_ || (p.y < m_.headerH && HeaderDivider(p.x))) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
            }
            break;
        case WM_KEYDOWN: OnKeyDown(wParam); return 0;
        case WM_CHAR: OnChar(static_cast<wchar_t>(wParam)); return 0;
        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lParam);
            if (hdr && hdr->hwndFrom == tip_ && hdr->code == TTN_GETDISPINFOW) {
                UpdateTooltipText(reinterpret_cast<NMTTDISPINFOW*>(lParam));
                return 0;
            }
            break;
        }
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

}  // namespace ft
