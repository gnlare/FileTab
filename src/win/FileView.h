// 파일 목록 컨트롤. 자세히 / 큰 아이콘(128px) / 보통 아이콘(64px)을 하나의 직접 그리기 창으로 구현한다.
// 보이는 행만 그리고(가상화), 아이콘은 ImageService 에서 즉시 받아 그린다.
#pragma once
#include "FolderTab.h"
#include "Images.h"
#include "Win.h"

namespace ft {

class FileView {
public:
    // 이 컨트롤을 소유한 패널이 구현한다.
    struct Host {
        virtual ~Host() = default;
        virtual void FvActivate() = 0;                   // 클릭/포커스: 활성 패널로 지정
        virtual void FvSelectionChanged() = 0;
        virtual void FvOpen() = 0;                       // 더블 클릭
        virtual void FvContextMenu(POINT screen) = 0;
        virtual void FvSortRequested(SortKey key) = 0;
    };

    static bool RegisterWindowClass(HINSTANCE instance);
    static FileView* Create(HWND parent, Host* host);
    ~FileView();

    HWND hwnd() const { return hwnd_; }

    void SetTab(std::shared_ptr<FolderTab> tab);          // 표시할 탭을 바꾼다(이전 탭의 선택·스크롤은 탭에 저장)
    void OnTabChanged(unsigned what);                     // 현재 탭의 변경 알림(TabChange 비트)
    void SaveStateToTab();
    void ApplyMetrics();                                  // DPI·여백 설정이 바뀌었을 때

    int SelectedCount() const { return selCount_; }
    std::wstring FirstSelectedName() const;
    std::vector<FileEntry> SelectedEntries() const;
    std::vector<std::wstring> SelectedPaths() const;
    void SelectOnlyByName(const std::wstring& name);      // 선택하고 화면에 보이게 한다
    bool HasFocus() const;
    void Focus();
    void RepaintNow();
    std::wstring FullPath(const FileEntry& e) const;

private:
    FileView() = default;
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // 배치
    struct Metrics {
        int rowH = 30, headerH = 26, iconPx = 64, tileW = 100, tileH = 100, nameIcon = 20;
    };
    void ComputeLayout();
    int ItemCount() const { return items_ ? static_cast<int>(items_->size()) : 0; }
    int ContentHeight() const;
    int MaxScroll() const;
    void UpdateScrollBar();
    void SetScroll(int y);
    void EnsureVisible(int index);
    int  HitTest(int x, int y) const;
    RECT ItemRect(int index) const;         // 클라이언트 좌표
    RECT ThumbRect() const;
    void ColumnLayout(int& nameW, int& sizeW, int& modW) const;
    int  HeaderDivider(int x) const;        // 1 = 이름|크기, 2 = 크기|수정, 0 = 없음
    int  HeaderColumn(int x) const;

    // 선택
    void ClearSelection();
    void SelectOnly(int i);
    void SelectRange(int a, int b);
    void ToggleSelect(int i);
    void NotifySelection();
    void MoveFocus(int target, bool extend, bool ctrlOnly);
    void RestoreSelection(const std::vector<std::wstring>& names, const std::wstring& focusName);

    // 그리기
    void Paint();
    void PaintDetails(HDC dc, uint64_t frame);
    void PaintTiles(HDC dc, uint64_t frame);
    void DrawSortArrow(HDC dc, int cx, int cy, bool descending);

    // 메시지 처리
    void OnLButtonDown(int x, int y, WPARAM keys);
    void OnMouseMove(int x, int y, WPARAM keys);
    void OnLButtonUp(int x, int y);
    void OnKeyDown(WPARAM key);
    void OnChar(wchar_t ch);
    void OnWheel(int delta);
    void UpdateTooltipText(NMTTDISPINFOW* info);

    HWND hwnd_ = nullptr;
    HWND tip_ = nullptr;
    Host* host_ = nullptr;
    std::shared_ptr<FolderTab> tab_;
    std::shared_ptr<const std::vector<FileEntry>> items_;
    std::wstring dir_;
    ViewMode mode_ = ViewMode::Details;
    Metrics m_;
    int cw_ = 0, ch_ = 0;
    int cols_ = 1;
    int scrollY_ = 0;

    std::vector<char> sel_;
    int selCount_ = 0;
    int anchor_ = -1, focus_ = -1;

    int sizeColDip_ = 110, modColDip_ = 160;      // 열 너비(DIP). 재시작 시 복원하지 않음(WPF 버전과 동일)
    static constexpr int kNameMinDip = 130;

    // 마우스 상태
    bool dragCandidate_ = false;
    POINT downPt_{0, 0};
    bool draggingThumb_ = false;
    int dragStartY_ = 0;
    int dragStartScroll_ = 0;
    int collapseTo_ = -1;       // 여러 항목 선택 중 클릭: 버튼을 뗄 때 이 항목만 선택
    int resizingCol_ = 0;       // 헤더 구분선 드래그 중인 열(1 또는 2)
    int resizeStartX_ = 0, resizeStartDip_ = 0;
    int headerDown_ = -1;       // 헤더 클릭 후보 열
    bool resized_ = false;
    int hot_ = -1;
    std::wstring tipText_;

    std::wstring typeBuf_;
    DWORD typeTick_ = 0;

    // 그리기 자원
    HDC memDc_ = nullptr, scratchDc_ = nullptr;
    HBITMAP backBmp_ = nullptr, backOld_ = nullptr;
    int backW_ = 0, backH_ = 0;
    EpochPtr epoch_ = std::make_shared<std::atomic<uint64_t>>(0);
};

}  // namespace ft
