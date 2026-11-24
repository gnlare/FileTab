// Shell 아이콘/썸네일 서비스.
//
// WPF 버전은 항목마다 SHCreateItemFromParsingName + GetImage 를 호출해 파일을 하나하나 열어 보았다.
// 이 버전은 다음 세 단계로 나눠서 첫 화면이 빈 칸 없이 즉시 그려지고 디스크 접근이 최소화되게 했다.
//   1) 일반 아이콘: 확장자(폴더는 하나)별로 시스템 이미지 목록에서 한 번만 만들어 모든 파일이 공유한다.
//      디스크를 읽지 않으므로 UI 스레드에서 즉시 얻는다.
//   2) 개별 이미지가 필요한 항목(exe·lnk·ico 등 파일마다 아이콘이 다른 형식, 썸네일 처리기가 있는 형식,
//      사용자 지정 폴더 아이콘)만 STA 작업 스레드에서 비동기로 가져온다. 도착하기 전에는 1)의 아이콘을 보여 준다.
//   3) 화면에서 사라진 항목의 대기 작업은 버려서 빠른 스크롤에서도 보이는 항목부터 처리된다.
#pragma once
#include "Win.h"

namespace ft {

struct Image {
    int w = 0, h = 0;
    HBITMAP bmp = nullptr;   // 32bpp 위에서 아래 방향 DIB, 알파 미리 곱함
    ~Image() {
        if (bmp) DeleteObject(bmp);
    }
};
using ImagePtr = std::shared_ptr<Image>;
using EpochPtr = std::shared_ptr<std::atomic<uint64_t>>;

class ImageService {
public:
    static ImageService& Get();

    void Init(HWND notifyWindow);   // WM_FT_IMAGES 를 받을 창과 작업 스레드를 준비
    void AckNotify();               // WM_FT_IMAGES 처리 후 호출(다음 알림 허용)

    // UI 스레드 전용. 항상 즉시 돌아오며(디스크 대기 없음) 그릴 수 있는 이미지를 돌려준다.
    // epoch/frame: 호출한 뷰의 그리기 세대. 세대가 바뀌면 아직 시작하지 않은 요청은 버려진다.
    ImagePtr ForEntry(const FileEntry& entry, const std::wstring& dir, int pixelSize, bool allowThumbnail,
                      const EpochPtr& epoch, uint64_t frame);

    // 이미지 한 장을 투명도를 살려 그린다(가운데 정렬, 크기 그대로).
    static void Draw(HDC dc, HDC scratchDc, const Image& image, const RECT& cell);

private:
    ImageService();
    struct Impl;
    Impl* impl_;
};

}  // namespace ft
