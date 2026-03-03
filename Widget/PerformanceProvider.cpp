#include "PerformanceProvider.h"

#include <algorithm>
#include <numeric>

namespace Widget {
    PerformanceProvider::PerformanceProvider() {
        QueryPerformanceFrequency(&mFrequency);
    }

    PerformanceProvider::~PerformanceProvider() {
        if (mAdapter3 != nullptr) {
            mAdapter3->Release();
            mAdapter3 = nullptr;
        }
    }

    PerformanceProvider& PerformanceProvider::Get() {
        static PerformanceProvider Instance{};
        return Instance;
    }

    void PerformanceProvider::Initialize(IDXGIAdapter1* Adapter) {
        if (mAdapter3 != nullptr) {
            mAdapter3->Release();
            mAdapter3 = nullptr;
        }

        if (Adapter != nullptr) {
            Adapter->QueryInterface(IID_PPV_ARGS(&mAdapter3));
        }
    }

    void PerformanceProvider::BeginFrame() {
        QueryPerformanceCounter(&mFrameBeginCounter);
        mHasFrameBegin = true;
        mCurrentFrameProfiles.clear();
        mActiveProfiles.clear();
    }

    void PerformanceProvider::EndFrame() {
        if (!mHasFrameBegin) {
            return;
        }

        LARGE_INTEGER EndCounter{};
        QueryPerformanceCounter(&EndCounter);

        const double DeltaMicroseconds{ static_cast<double>(EndCounter.QuadPart - mFrameBeginCounter.QuadPart) * 1000000.0 / static_cast<double>(mFrequency.QuadPart) };
        mFrameTimeMicroseconds.PushBack(static_cast<float>(DeltaMicroseconds));

        UpdateVramInfoIfNeeded();
        UpdatePercentileCache();

        mHasFrameBegin = false;
    }

    void PerformanceProvider::BeginProfile(const std::string& Name) {
        ActiveProfile Entry{};
        Entry.Name = Name;
        Entry.StartMicroseconds = QueryNowMicroseconds();
        mActiveProfiles.push_back(Entry);
    }

    void PerformanceProvider::EndProfile() {
        if (mActiveProfiles.empty()) {
            return;
        }

        const ActiveProfile Entry{ mActiveProfiles.back() };
        mActiveProfiles.pop_back();

        ProfileEntry Result{};
        Result.Name = Entry.Name;
        Result.StartMicroseconds = Entry.StartMicroseconds;
        Result.EndMicroseconds = QueryNowMicroseconds();
        Result.Depth = mActiveProfiles.size();
        mCurrentFrameProfiles.push_back(Result);
    }

    std::vector<float> PerformanceProvider::GetFrameTimeMilliseconds() const {
        std::vector<float> Values{};
        const std::vector<float> Microseconds{ mFrameTimeMicroseconds.ToVector() };
        Values.reserve(Microseconds.size());

        for (float Value : Microseconds) {
            Values.push_back(Value / 1000.0f);
        }

        return Values;
    }

    double PerformanceProvider::GetAverageFps() const {
        return mAverageFps;
    }

    double PerformanceProvider::GetOnePercentLowFps() const {
        return mOnePercentLowFps;
    }

    double PerformanceProvider::GetZeroPointOnePercentLowFps() const {
        return mZeroPointOnePercentLowFps;
    }

    std::vector<ProfileEntry> PerformanceProvider::GetCurrentFrameProfiles() const {
        return mCurrentFrameProfiles;
    }

    uint64_t PerformanceProvider::GetVramBudgetBytes() const {
        return mVramBudgetBytes;
    }

    uint64_t PerformanceProvider::GetVramUsageBytes() const {
        return mVramUsageBytes;
    }

    float PerformanceProvider::GetVramUsageRatio() const {
        if (mVramBudgetBytes == 0) {
            return 0.0f;
        }

        return static_cast<float>(static_cast<double>(mVramUsageBytes) / static_cast<double>(mVramBudgetBytes));
    }

    void PerformanceProvider::UpdatePercentileCache() {
        const double NowMicroseconds{ QueryNowMicroseconds() };

        if (NowMicroseconds - mLastPercentileUpdateMicroseconds < 1000000.0) {
            return;
        }

        mLastPercentileUpdateMicroseconds = NowMicroseconds;

        std::vector<float> Samples{ mFrameTimeMicroseconds.ToVector() };
        if (Samples.empty()) {
            mAverageFps = 0.0;
            mOnePercentLowFps = 0.0;
            mZeroPointOnePercentLowFps = 0.0;
            return;
        }

        const double SumMicroseconds{ std::accumulate(Samples.begin(), Samples.end(), 0.0) };
        const double AverageFrameMicroseconds{ SumMicroseconds / static_cast<double>(Samples.size()) };
        mAverageFps = AverageFrameMicroseconds > 0.0 ? 1000000.0 / AverageFrameMicroseconds : 0.0;

        std::sort(Samples.begin(), Samples.end());

        const std::size_t OnePercentIndex{ Samples.size() > 1 ? static_cast<std::size_t>(static_cast<double>(Samples.size() - 1) * 0.99) : 0 };
        const std::size_t ZeroPointOnePercentIndex{ Samples.size() > 1 ? static_cast<std::size_t>(static_cast<double>(Samples.size() - 1) * 0.999) : 0 };

        const double OnePercentFrameMicroseconds{ static_cast<double>(Samples[OnePercentIndex]) };
        const double ZeroPointOneFrameMicroseconds{ static_cast<double>(Samples[ZeroPointOnePercentIndex]) };

        mOnePercentLowFps = OnePercentFrameMicroseconds > 0.0 ? 1000000.0 / OnePercentFrameMicroseconds : 0.0;
        mZeroPointOnePercentLowFps = ZeroPointOneFrameMicroseconds > 0.0 ? 1000000.0 / ZeroPointOneFrameMicroseconds : 0.0;
    }

    void PerformanceProvider::UpdateVramInfoIfNeeded() {
        if (mAdapter3 == nullptr) {
            return;
        }

        const double NowMicroseconds{ QueryNowMicroseconds() };
        if (NowMicroseconds - mLastVramUpdateMicroseconds < 500000.0) {
            return;
        }

        mLastVramUpdateMicroseconds = NowMicroseconds;

        DXGI_QUERY_VIDEO_MEMORY_INFO Info{};
        if (SUCCEEDED(mAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &Info))) {
            mVramBudgetBytes = Info.Budget;
            mVramUsageBytes = Info.CurrentUsage;
        }
    }

    double PerformanceProvider::QueryNowMicroseconds() const {
        LARGE_INTEGER Counter{};
        QueryPerformanceCounter(&Counter);
        return static_cast<double>(Counter.QuadPart) * 1000000.0 / static_cast<double>(mFrequency.QuadPart);
    }
}
