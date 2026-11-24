#include "Images.h"

#include <commoncontrols.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <limits>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "../core/LruCache.h"
#include "Workers.h"

namespace ft {

namespace {

// ---------- 픽셀 도우미 (0xAARRGGBB, 알파 미리 곱한 형식) ----------

bool GetBits(HBITMAP bmp, int w, int h, std::vector<uint32_t>& out) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // 위에서 아래
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    out.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
    HDC dc = GetDC(nullptr);
    int rows = GetDIBits(dc, bmp, 0, static_cast<UINT>(h), out.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    return rows == h;
}

bool AnyAlpha(const std::vector<uint32_t>& px) {
    for (uint32_t p : px)
        if ((p >> 24) != 0) return true;
    return false;
}

void Premultiply(std::vector<uint32_t>& px) {
    for (uint32_t& p : px) {
        uint32_t a = p >> 24;
        if (a == 255) continue;
        uint32_t r = ((p >> 16) & 255) * a / 255;
        uint32_t g = ((p >> 8) & 255) * a / 255;
        uint32_t b = (p & 255) * a / 255;
        p = (a << 24) | (r << 16) | (g << 8) | b;
    }
}

// 면적 평균 방식 크기 변경(축소 품질이 좋다). 입력·출력 모두 미리 곱한 알파.
std::vector<uint32_t> Resize(const std::vector<uint32_t>& src, int sw, int sh, int dw, int dh) {
    if (sw == dw && sh == dh) return src;
    std::vector<uint32_t> dst(static_cast<size_t>(dw) * static_cast<size_t>(dh));
    const double sx = static_cast<double>(sw) / dw, sy = static_cast<double>(sh) / dh;
    for (int y = 0; y < dh; ++y) {
        const double y0 = y * sy, y1 = (y + 1) * sy;
        const int iy0 = static_cast<int>(std::floor(y0));
        const int iy1 = std::min(sh - 1, static_cast<int>(std::ceil(y1)) - 1);
        for (int x = 0; x < dw; ++x) {
            const double x0 = x * sx, x1 = (x + 1) * sx;
            const int ix0 = static_cast<int>(std::floor(x0));
            const int ix1 = std::min(sw - 1, static_cast<int>(std::ceil(x1)) - 1);
            double a = 0, r = 0, g = 0, b = 0, wsum = 0;
            for (int iy = iy0; iy <= iy1; ++iy) {
                const double wy = std::min(y1, iy + 1.0) - std::max(y0, static_cast<double>(iy));
                for (int ix = ix0; ix <= ix1; ++ix) {
                    const double wx = std::min(x1, ix + 1.0) - std::max(x0, static_cast<double>(ix));
                    const double w = wx * wy;
                    const uint32_t p = src[static_cast<size_t>(iy) * static_cast<size_t>(sw) + static_cast<size_t>(ix)];
                    a += w * (p >> 24);
                    r += w * ((p >> 16) & 255);
                    g += w * ((p >> 8) & 255);
                    b += w * (p & 255);
                    wsum += w;
                }
            }
            if (wsum <= 0) wsum = 1;
            auto q = [&](double v) { return static_cast<uint32_t>(std::min(255.0, v / wsum + 0.5)); };
            dst[static_cast<size_t>(y) * static_cast<size_t>(dw) + static_cast<size_t>(x)] = (q(a) << 24) | (q(r) << 16) | (q(g) << 8) | q(b);
        }
    }
    return dst;
}

ImagePtr MakeImage(const std::vector<uint32_t>& px, int w, int h) {
    // Never write beyond a DIB if a shell handler returns inconsistent image dimensions.
    if (w <= 0 || h <= 0 || static_cast<size_t>(w) > std::numeric_limits<size_t>::max() / static_cast<size_t>(h) ||
        px.size() != static_cast<size_t>(w) * static_cast<size_t>(h)) return nullptr;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        return nullptr;
    }
    memcpy(bits, px.data(), px.size() * sizeof(uint32_t));
    auto image = std::make_shared<Image>();
    image->w = w;
    image->h = h;
    image->bmp = bmp;
    return image;
}

// HICON → 정사각형 size×size 이미지
ImagePtr FromIcon(HICON icon, int size) {
    ICONINFO ii{};
    if (!GetIconInfo(icon, &ii)) return nullptr;
    ImagePtr result;
    HBITMAP probe = ii.hbmColor ? ii.hbmColor : ii.hbmMask;
    BITMAP bm{};
    if (probe && GetObjectW(probe, sizeof bm, &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
        int w = bm.bmWidth;
        int h = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
        std::vector<uint32_t> px;
        if (ii.hbmColor && GetBits(ii.hbmColor, w, h, px)) {
            if (!AnyAlpha(px) && ii.hbmMask) {
                // 알파가 없는 옛 아이콘: AND 마스크로 투명 영역을 만든다.
                std::vector<uint32_t> mask;
                if (GetBits(ii.hbmMask, w, h, mask))
                    for (size_t i = 0; i < px.size(); ++i) px[i] = (px[i] & 0x00FFFFFFu) | ((mask[i] & 0x00FFFFFFu) == 0 ? 0xFF000000u : 0u);
                else
                    for (auto& p : px) p |= 0xFF000000u;
            }
            Premultiply(px);
            result = MakeImage(Resize(px, w, h, size, size), size, size);
        }
    }
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    return result;
}

// Shell 이 돌려준 HBITMAP(알파 포함 32bpp) → 이미지(크기 그대로)
ImagePtr FromShellBitmap(HBITMAP bmp) {
    BITMAP bm{};
    if (!GetObjectW(bmp, sizeof bm, &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return nullptr;
    int w = bm.bmWidth, h = bm.bmHeight;
    std::vector<uint32_t> px;
    if (!GetBits(bmp, w, h, px)) return nullptr;
    if (!AnyAlpha(px))
        for (auto& p : px) p |= 0xFF000000u;   // 알파 정보가 없으면 불투명으로 취급
    Premultiply(px);
    return MakeImage(px, w, h);
}

// ---------- 확장자 분류 ----------

bool KeyExists(const std::wstring& sub) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, sub.c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegCloseKey(k);
        return true;
    }
    return false;
}

std::wstring RegDefault(const std::wstring& sub) {
    wchar_t buf[512];
    DWORD size = sizeof buf;
    if (RegGetValueW(HKEY_CLASSES_ROOT, sub.c_str(), nullptr, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, buf, &size) ==
        ERROR_SUCCESS)
        return buf;
    return std::wstring();
}

struct ExtInfo {
    bool perFileIcon = false;   // 파일마다 아이콘이 다른 형식(exe, lnk, ico …)
    bool thumbnail = false;     // 썸네일 처리기가 있는 형식
};

ExtInfo Classify(const std::wstring& ext) {
    ExtInfo info;
    if (ext.empty()) return info;
    static const wchar_t* kPerFile[] = {L".exe", L".lnk", L".ico", L".cur", L".ani", L".scr", L".url", L".icl", L".appref-ms"};
    for (const wchar_t* e : kPerFile)
        if (ext == e) info.perFileIcon = true;
    std::wstring progId = RegDefault(ext);
    if (!info.perFileIcon && !progId.empty()) {
        std::wstring icon = RegDefault(progId + L"\\DefaultIcon");
        if (icon.find(L"%1") != std::wstring::npos) info.perFileIcon = true;
    }
    if (info.perFileIcon) return info;

    static const wchar_t* kThumbHandler = L"\\ShellEx\\{E357FCCD-A995-4576-B01F-234630154E96}";
    static const wchar_t* kExtractImage = L"\\ShellEx\\{BB2E617C-0920-11D1-9A0B-00C04FC2D6C1}";
    std::vector<std::wstring> candidates = {ext, L"SystemFileAssociations\\" + ext};
    if (!progId.empty()) candidates.push_back(progId);
    PERCEIVED type = PERCEIVED_TYPE_UNSPECIFIED;
    PERCEIVEDFLAG flags = 0;
    if (SUCCEEDED(AssocGetPerceivedType(ext.c_str(), &type, &flags, nullptr))) {
        if (type == PERCEIVED_TYPE_IMAGE || type == PERCEIVED_TYPE_VIDEO) {
            info.thumbnail = true;
            return info;
        }
        const wchar_t* name = nullptr;
        switch (type) {
            case PERCEIVED_TYPE_AUDIO: name = L"audio"; break;
            case PERCEIVED_TYPE_DOCUMENT: name = L"document"; break;
            case PERCEIVED_TYPE_TEXT: name = L"text"; break;
            default: break;
        }
        if (name) candidates.push_back(std::wstring(L"SystemFileAssociations\\") + name);
    }
    for (const auto& c : candidates)
        if (KeyExists(c + kThumbHandler) || KeyExists(c + kExtractImage)) {
            info.thumbnail = true;
            break;
        }
    return info;
}

}  // namespace

// ======================= ImageService =======================

struct ImageService::Impl {
    struct Job {
        uint64_t key = 0;
        std::wstring path;
        int px = 0;
        bool thumb = false;
        EpochPtr epoch;
        uint64_t frame = 0;
    };

    HWND notifyWnd = nullptr;
    std::vector<StaWorker*> workers;

    // UI 스레드 전용
    std::unordered_map<uint64_t, ImagePtr> generic;
    std::unordered_map<uint32_t, ExtInfo> extInfo;

    // 여러 스레드가 공유
    std::mutex cacheMutex;
    LruCache<uint64_t, ImagePtr> cache{96ull * 1024 * 1024};   // 개별 이미지(썸네일·고유 아이콘). 널 = 가져오기 실패
    std::mutex jobMutex;
    std::unordered_map<uint64_t, Job> jobs;
    std::deque<uint64_t> order;
    std::unordered_set<uint64_t> active;   // 지금 가져오는 중인 키(같은 이미지를 중복 요청하지 않기 위함)
    std::atomic<bool> notifyPending{false};

    const ExtInfo& InfoFor(uint32_t extId) {
        auto it = extInfo.find(extId);
        if (it != extInfo.end()) return it->second;
        return extInfo.emplace(extId, Classify(ExtensionOfId(extId))).first->second;
    }

    ImagePtr Generic(const FileEntry& e, int px) {
        uint64_t key = (static_cast<uint64_t>(e.IsDir() ? 0xFFFFFFFFu : e.extId) << 32) | static_cast<uint32_t>(px);
        auto it = generic.find(key);
        if (it != generic.end()) return it->second;
        if (generic.size() > 800) generic.clear();   // 확장자 종류가 비정상적으로 많을 때의 안전장치
        std::wstring name = e.IsDir() ? L"folder" : L"x" + ExtensionOfId(e.extId);
        DWORD attrs = e.IsDir() ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        ImagePtr image;
        SHFILEINFOW sfi{};
        if (SHGetFileInfoW(name.c_str(), attrs, &sfi, sizeof sfi, SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES)) {
            int list = px <= 16 ? SHIL_SMALL : px <= 32 ? SHIL_LARGE : px <= 48 ? SHIL_EXTRALARGE : SHIL_JUMBO;
            IImageList* il = nullptr;
            if (SUCCEEDED(SHGetImageList(list, IID_PPV_ARGS(&il))) && il) {
                HICON icon = nullptr;
                if (SUCCEEDED(il->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &icon)) && icon) {
                    image = FromIcon(icon, px);
                    DestroyIcon(icon);
                }
                il->Release();
            }
        }
        generic[key] = image;
        return image;
    }

    bool TryGet(uint64_t key, ImagePtr& out) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        ImagePtr* hit = cache.Get(key);
        if (!hit) return false;
        out = *hit;
        return true;
    }

    void Request(Job job) {
        {
            std::lock_guard<std::mutex> lock(jobMutex);
            if (active.count(job.key)) return;
            auto it = jobs.find(job.key);
            if (it != jobs.end()) {   // 이미 대기 중: 최신 그리기 세대로 갱신해 버려지지 않게 한다
                it->second.epoch = job.epoch;
                it->second.frame = job.frame;
                return;
            }
            order.push_back(job.key);
            jobs.emplace(job.key, std::move(job));
        }
        StaWorker* best = workers[0];
        for (StaWorker* w : workers)
            if (w->Pending() < best->Pending()) best = w;
        best->Post([this] { DrainOne(); });
    }

    // 작업 스레드(STA)에서 실행
    void DrainOne() {
        Job job;
        bool got = false;
        {
            std::lock_guard<std::mutex> lock(jobMutex);
            while (!order.empty()) {
                uint64_t key = order.front();
                order.pop_front();
                auto it = jobs.find(key);
                if (it == jobs.end()) continue;
                Job candidate = std::move(it->second);
                jobs.erase(it);
                if (candidate.epoch && candidate.epoch->load() != candidate.frame) continue;   // 화면에서 사라짐: 버림
                job = std::move(candidate);
                active.insert(job.key);
                got = true;
                break;
            }
        }
        if (!got) return;
        ImagePtr image = Load(job);
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            size_t bytes = image ? static_cast<size_t>(image->w) * static_cast<size_t>(image->h) * 4 : 64;
            cache.Put(job.key, image, bytes);
        }
        {
            std::lock_guard<std::mutex> lock(jobMutex);
            active.erase(job.key);
        }
        if (!notifyPending.exchange(true) && notifyWnd) PostMessageW(notifyWnd, WM_FT_IMAGES, 0, 0);
    }

    static ImagePtr Load(const Job& job) {
        IShellItemImageFactory* factory = nullptr;
        if (FAILED(SHCreateItemFromParsingName(job.path.c_str(), nullptr, IID_PPV_ARGS(&factory))) || !factory) return nullptr;
        HBITMAP bmp = nullptr;
        SIZE size{job.px, job.px};
        HRESULT hr = factory->GetImage(size, job.thumb ? SIIGBF_THUMBNAILONLY : SIIGBF_ICONONLY, &bmp);
        factory->Release();
        if (FAILED(hr) || !bmp) return nullptr;
        ImagePtr image = FromShellBitmap(bmp);
        DeleteObject(bmp);
        return image;
    }
};

ImageService::ImageService() : impl_(new Impl()) {}

ImageService& ImageService::Get() {
    static ImageService* instance = new ImageService();   // 프로세스 종료까지 유지
    return *instance;
}

void ImageService::Init(HWND notifyWindow) {
    impl_->notifyWnd = notifyWindow;
    if (!impl_->workers.empty()) return;
    unsigned hw = std::thread::hardware_concurrency();
    int count = static_cast<int>(std::min(6u, std::max(2u, hw / 2)));
    for (int i = 0; i < count; ++i) impl_->workers.push_back(new StaWorker());
}

void ImageService::AckNotify() { impl_->notifyPending.store(false); }

ImagePtr ImageService::ForEntry(const FileEntry& e, const std::wstring& dir, int px, bool allowThumbnail, const EpochPtr& epoch,
                                uint64_t frame) {
    bool specific = false, thumb = false;
    if (e.IsDir()) {
        // 읽기 전용/시스템 속성이 붙은 폴더만 desktop.ini 사용자 지정 아이콘을 가질 수 있다(탐색기와 같은 규칙).
        specific = (e.attrs & (kAttrReadOnly | kAttrSystem)) != 0;
    } else {
        const ExtInfo& info = impl_->InfoFor(e.extId);
        if (info.perFileIcon) specific = true;
        else if (allowThumbnail && info.thumbnail) specific = thumb = true;
    }
    ImagePtr base = impl_->Generic(e, px);
    if (!specific || impl_->workers.empty()) return base;

    uint64_t key = e.IsDir() ? HashCombine(e.pathHash, static_cast<uint64_t>(px) | (1ull << 40))
                             : HashCombine(HashCombine(e.pathHash, static_cast<uint64_t>(e.modified)),
                                           e.size ^ (static_cast<uint64_t>(px) << 40) ^ (thumb ? 1ull : 2ull));
    ImagePtr hit;
    if (impl_->TryGet(key, hit)) return hit ? hit : base;

    Impl::Job job;
    job.key = key;
    job.path = path::Combine(dir, e.name);
    job.px = px;
    job.thumb = thumb;
    job.epoch = epoch;
    job.frame = frame;
    impl_->Request(std::move(job));
    return base;
}

void ImageService::Draw(HDC dc, HDC scratchDc, const Image& image, const RECT& cell) {
    if (!image.bmp) return;
    int x = cell.left + ((cell.right - cell.left) - image.w) / 2;
    int y = cell.top + ((cell.bottom - cell.top) - image.h) / 2;
    HGDIOBJ old = SelectObject(scratchDc, image.bmp);
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, x, y, image.w, image.h, scratchDc, 0, 0, image.w, image.h, bf);
    SelectObject(scratchDc, old);
}

}  // namespace ft
