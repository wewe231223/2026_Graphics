#include "PerformanceProvider.h"

#include <algorithm>
#include <numeric>
#include <utility>

#ifdef max
#undef max
#endif

namespace Widget {
    constexpr double FrameTimeHistoryMicroseconds{ 2000000.0 };
    constexpr double TimelineAverageMicroseconds{ 200000.0 };

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
        mHasFrameBegin = true;
        mFrameBeginMicroseconds = QueryNowMicroseconds();
        mCurrentFrameIdentifier += 1;
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

        const double EndMicroseconds{ QueryNowMicroseconds() };
        const double DeltaMicroseconds{ std::max(0.0, EndMicroseconds - mFrameBeginMicroseconds) };
        FrameTimeRecord FrameTimeRecord{};
        FrameTimeRecord.mFrameIdentifier = mCurrentFrameIdentifier;
        FrameTimeRecord.mEndMicroseconds = EndMicroseconds;
        FrameTimeRecord.mCpuTimeMicroseconds = static_cast<float>(DeltaMicroseconds);
        mFrameTimeRecords.push_back(FrameTimeRecord);
        PruneOldFrameTimeRecords(EndMicroseconds);

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
        UpdateTimelineAverageProfiles(NewFrameProfiles, EndMicroseconds);
        mCurrentFrameProfiles = std::move(NewFrameProfiles);

        mHasFrameBegin = false;
    }

    void PerformanceProvider::SubmitGpuFrameTime(std::uint64_t FrameIdentifier, double GpuTimeMicroseconds) {
        if (FrameIdentifier == 0) {
            return;
        }

        const std::vector<FrameTimeRecord>::reverse_iterator FoundIterator{ std::find_if(mFrameTimeRecords.rbegin(), mFrameTimeRecords.rend(), [FrameIdentifier](const FrameTimeRecord& FrameTimeRecord) {
            return FrameTimeRecord.mFrameIdentifier == FrameIdentifier;
        }) };
        if (FoundIterator == mFrameTimeRecords.rend()) {
            return;
        }

        FoundIterator->mGpuTimeMicroseconds = static_cast<float>(std::max(0.0, GpuTimeMicroseconds));
        FoundIterator->mHasGpuTime = true;
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

    std::uint64_t PerformanceProvider::GetCurrentFrameIdentifier() const {
        return mCurrentFrameIdentifier;
    }

    std::vector<float> PerformanceProvider::GetCpuFrameTimeMilliseconds() const {
        std::vector<float> Values{};
        const std::vector<FrameTimeSample> Samples{ GetFrameTimeSamples() };
        Values.reserve(Samples.size());

        for (const FrameTimeSample& Sample : Samples) {
            Values.push_back(Sample.mCpuTimeMilliseconds);
        }

        return Values;
    }

    std::vector<float> PerformanceProvider::GetGpuFrameTimeMilliseconds() const {
        std::vector<float> Values{};
        const std::vector<FrameTimeSample> Samples{ GetFrameTimeSamples() };
        Values.reserve(Samples.size());

        for (const FrameTimeSample& Sample : Samples) {
            if (Sample.mHasGpuTime == false) {
                continue;
            }

            Values.push_back(Sample.mGpuTimeMilliseconds);
        }

        return Values;
    }

    std::vector<FrameTimeSample> PerformanceProvider::GetFrameTimeSamples() const {
        std::vector<FrameTimeSample> Samples{};
        const double NowMicroseconds{ QueryNowMicroseconds() };
        Samples.reserve(mFrameTimeRecords.size());

        for (const FrameTimeRecord& FrameTimeRecord : mFrameTimeRecords) {
            const double AgeMicroseconds{ NowMicroseconds - FrameTimeRecord.mEndMicroseconds };
            if (AgeMicroseconds > FrameTimeHistoryMicroseconds) {
                continue;
            }

            FrameTimeSample Sample{};
            Sample.mAgeSeconds = static_cast<float>(std::max(0.0, AgeMicroseconds) / 1000000.0);
            Sample.mCpuTimeMilliseconds = FrameTimeRecord.mCpuTimeMicroseconds / 1000.0f;
            Sample.mGpuTimeMilliseconds = FrameTimeRecord.mGpuTimeMicroseconds / 1000.0f;
            Sample.mHasGpuTime = FrameTimeRecord.mHasGpuTime;
            Samples.push_back(Sample);
        }

        return Samples;
    }

    float PerformanceProvider::GetFrameTimeHistorySeconds() const {
        return static_cast<float>(FrameTimeHistoryMicroseconds / 1000000.0);
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

    std::vector<ProfileEntry> PerformanceProvider::GetTimelineAverageProfiles() const {
        return mTimelineAverageProfiles;
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

    void PerformanceProvider::PruneOldFrameTimeRecords(double NowMicroseconds) {
        const std::vector<FrameTimeRecord>::iterator FirstValidIterator{ std::find_if(mFrameTimeRecords.begin(), mFrameTimeRecords.end(), [NowMicroseconds](const FrameTimeRecord& FrameTimeRecord) {
            return NowMicroseconds - FrameTimeRecord.mEndMicroseconds <= FrameTimeHistoryMicroseconds;
        }) };
        mFrameTimeRecords.erase(mFrameTimeRecords.begin(), FirstValidIterator);
    }

    void PerformanceProvider::UpdateTimelineAverageProfiles(const std::vector<ProfileEntry>& Entries, double EndMicroseconds) {
        if (mTimelineProfileFrameCount == 0) {
            mTimelineProfileAverageBeginMicroseconds = EndMicroseconds;
        }

        for (const ProfileEntry& Entry : Entries) {
            const double DurationMicroseconds{ std::max(0.0, Entry.EndMicroseconds - Entry.StartMicroseconds) };
            bool IsFound{};
            for (std::pair<std::string, double>& DurationSum : mTimelineProfileDurationSums) {
                if (DurationSum.first == Entry.Name) {
                    DurationSum.second += DurationMicroseconds;
                    IsFound = true;
                    break;
                }
            }

            if (!IsFound) {
                mTimelineProfileDurationSums.push_back(std::pair<std::string, double>{ Entry.Name, DurationMicroseconds });
            }
        }

        mTimelineProfileFrameCount += 1;

        const double ElapsedMicroseconds{ EndMicroseconds - mTimelineProfileAverageBeginMicroseconds };
        if (ElapsedMicroseconds < TimelineAverageMicroseconds) {
            return;
        }

        std::vector<ProfileEntry> AverageProfiles{};
        AverageProfiles.reserve(mTimelineProfileDurationSums.size());

        const double FrameCount{ static_cast<double>(mTimelineProfileFrameCount) };
        double CurrentStartMicroseconds{};
        for (const std::pair<std::string, double>& DurationSum : mTimelineProfileDurationSums) {
            ProfileEntry Entry{};
            Entry.Name = DurationSum.first;
            Entry.StartMicroseconds = CurrentStartMicroseconds;
            Entry.EndMicroseconds = CurrentStartMicroseconds + DurationSum.second / FrameCount;
            Entry.Depth = 0;
            AverageProfiles.push_back(Entry);
            CurrentStartMicroseconds = Entry.EndMicroseconds;
        }

        mTimelineAverageProfiles = std::move(AverageProfiles);
        mTimelineProfileDurationSums.clear();
        mTimelineProfileFrameCount = 0;
        mTimelineProfileAverageBeginMicroseconds = EndMicroseconds;
    }

    void PerformanceProvider::UpdatePercentileCache() {
        const double NowMicroseconds{ QueryNowMicroseconds() };

        if (NowMicroseconds - mLastPercentileUpdateMicroseconds < 1000000.0) {
            return;
        }

        mLastPercentileUpdateMicroseconds = NowMicroseconds;

        std::vector<float> Samples{};
        Samples.reserve(mFrameTimeRecords.size());

        for (const FrameTimeRecord& FrameTimeRecord : mFrameTimeRecords) {
            Samples.push_back(FrameTimeRecord.mCpuTimeMicroseconds);
        }

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
