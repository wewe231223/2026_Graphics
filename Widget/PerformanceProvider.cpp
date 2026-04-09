#include "PerformanceProvider.h"

#include <algorithm>
#include <numeric>
#include <utility>

#ifdef max
#undef max
#endif

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
        mFrameBeginMicroseconds = QueryNowMicroseconds();
        mPhaseDurations.clear();
        mActivePhaseName.clear();
        mActivePhaseStartMicroseconds = 0.0;
        mHasActivePhase = false;
    }

    void PerformanceProvider::EndFrame() {
        if (!mHasFrameBegin) {
            return;
        }
        if (mHasActivePhase) {
            EndPhaseProfile();
        }

        LARGE_INTEGER EndCounter{};
        QueryPerformanceCounter(&EndCounter);

        const double DeltaMicroseconds{ static_cast<double>(EndCounter.QuadPart - mFrameBeginCounter.QuadPart) * 1000000.0 / static_cast<double>(mFrequency.QuadPart) };
        mFrameTimeMicroseconds.PushBack(static_cast<float>(DeltaMicroseconds));

        UpdateVramInfoIfNeeded();
        UpdatePercentileCache();

        std::vector<ProfileEntry> NewFrameProfiles{};
        NewFrameProfiles.reserve(mPhaseDurations.size());
        double CurrentStartMicroseconds{ mFrameBeginMicroseconds };
        for (const std::pair<std::string, double>& PhaseDuration : mPhaseDurations) {
            ProfileEntry Entry{};
            Entry.Name = PhaseDuration.first;
            Entry.StartMicroseconds = CurrentStartMicroseconds;
            Entry.EndMicroseconds = CurrentStartMicroseconds + PhaseDuration.second;
            Entry.Depth = 0;
            NewFrameProfiles.push_back(Entry);
            CurrentStartMicroseconds = Entry.EndMicroseconds;
        }
        mCurrentFrameProfiles = std::move(NewFrameProfiles);

        mHasFrameBegin = false;
    }

    void PerformanceProvider::BeginPhaseProfile(const std::string& Name) {
        if (!mHasFrameBegin || mHasActivePhase) {
            return;
        }

        mActivePhaseName = Name;
        mActivePhaseStartMicroseconds = QueryNowMicroseconds();
        mHasActivePhase = true;
    }

    void PerformanceProvider::EndPhaseProfile() {
        if (!mHasFrameBegin || !mHasActivePhase) {
            return;
        }

        const double EndMicroseconds{ QueryNowMicroseconds() };
        const double DurationMicroseconds{ std::max(0.0, EndMicroseconds - mActivePhaseStartMicroseconds) };
        bool IsFound{};
        for (std::pair<std::string, double>& PhaseDuration : mPhaseDurations) {
            if (PhaseDuration.first == mActivePhaseName) {
                PhaseDuration.second += DurationMicroseconds;
                IsFound = true;
                break;
            }
        }
        if (!IsFound) {
            mPhaseDurations.push_back(std::make_pair(mActivePhaseName, DurationMicroseconds));
        }

        mActivePhaseName.clear();
        mActivePhaseStartMicroseconds = 0.0;
        mHasActivePhase = false;
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
