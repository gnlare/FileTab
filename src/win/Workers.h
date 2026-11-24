// 백그라운드 스레드 도우미.
//  - StaWorker : 메시지 루프를 가진 전용 STA 스레드(Shell COM 이 요구함). 파일 작업·썸네일 추출용.
//  - ThreadPool: COM 이 필요 없는 일반 작업(폴더 읽기)용 작은 풀.
// 두 클래스 모두 프로세스 종료까지 살아 있도록 설계했다(소멸자에서 join 하지 않는다).
// 느린 네트워크 Shell 호출이 종료를 막지 않게 하려는 것이며, 앱은 종료 시 ExitProcess 로 끝낸다.
#pragma once
#include "Win.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace ft {

class StaWorker {
public:
    StaWorker() : sem_(CreateSemaphoreW(nullptr, 0, 0x7FFFFFFF, nullptr)) {
        std::thread([this] { Run(); }).detach();
    }
    StaWorker(const StaWorker&) = delete;
    StaWorker& operator=(const StaWorker&) = delete;

    void Post(std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lock(m_);
            q_.push_back(std::move(fn));
        }
        pending_.fetch_add(1);
        ReleaseSemaphore(sem_, 1, nullptr);
    }
    int Pending() const { return pending_.load(); }

private:
    void Run() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        for (;;) {
            DWORD r = MsgWaitForMultipleObjectsEx(1, &sem_, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (r == WAIT_OBJECT_0) {
                std::function<void()> fn;
                {
                    std::lock_guard<std::mutex> lock(m_);
                    if (!q_.empty()) {
                        fn = std::move(q_.front());
                        q_.pop_front();
                    }
                }
                if (fn) fn();
                pending_.fetch_sub(1);
            } else if (r == WAIT_OBJECT_0 + 1) {
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            } else {
                break;  // 세마포어 핸들 오류
            }
        }
    }

    HANDLE sem_;
    std::mutex m_;
    std::deque<std::function<void()>> q_;
    std::atomic<int> pending_{0};
};

class ThreadPool {
public:
    explicit ThreadPool(int threads) {
        for (int i = 0; i < threads; ++i) std::thread([this] { Run(); }).detach();
    }
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void Post(std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lock(m_);
            q_.push_back(std::move(fn));
        }
        cv_.notify_one();
    }

private:
    void Run() {
        for (;;) {
            std::function<void()> fn;
            {
                std::unique_lock<std::mutex> lock(m_);
                cv_.wait(lock, [this] { return !q_.empty(); });
                fn = std::move(q_.front());
                q_.pop_front();
            }
            fn();
        }
    }
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> q_;
};

}  // namespace ft
