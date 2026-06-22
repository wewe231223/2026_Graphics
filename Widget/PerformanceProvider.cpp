#include "PerformanceProvider.h"

#include <algorithm>
#include <numeric>
#include <tuple>
#include <utility>

#ifdef max
#undef max
#endif

#ifdef min
#undef min
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
        mActiveProfiles.clear();
        mCurrentFrameProfiles.clear();
    }

    void PerformanceProvider::EndFrame() {
        if (!mHasFrameBegin) {
            return;
        }
        while (mActiveProfiles.empty() == false) {
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

        std::sort(mCurrentFrameProfiles.begin(), mCurrentFrameProfiles.end(), [](const ProfileEntry& Left, const ProfileEntry& Right) {
            return std::tie(Left.StartMicroseconds, Left.Depth) < std::tie(Right.StartMicroseconds, Right.Depth);
        });
        UpdateTimelineAverageProfiles(mCurrentFrameProfiles, EndMicroseconds);

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
        if (!mHasFrameBegin) {
            return;
        }

        ActiveProfile Profile{};
        Profile.mName = Name;
        Profile.mStartMicroseconds = QueryNowMicroseconds();
        Profile.mDepth = mActiveProfiles.size();
        mActiveProfiles.push_back(std::move(Profile));
    }

    void PerformanceProvider::EndPhaseProfile() {
        if (!mHasFrameBegin || mActiveProfiles.empty()) {
            return;
        }

        ActiveProfile Profile{ std::move(mActiveProfiles.back()) };
        mActiveProfiles.pop_back();
        const double EndMicroseconds{ QueryNowMicroseconds() };
        ProfileEntry Entry{};
        Entry.Name = std::move(Profile.mName);
        Entry.StartMicroseconds = Profile.mStartMicroseconds;
        Entry.EndMicroseconds = std::max(Profile.mStartMicroseconds, EndMicroseconds);
        Entry.Depth = Profile.mDepth;
        mCurrentFrameProfiles.push_back(std::move(Entry));
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

        for (std::size_t EntryIndex{}; EntryIndex < Entries.size(); EntryIndex += 1ULL) {
            const ProfileEntry& Entry{ Entries[EntryIndex] };
            const double DurationMicroseconds{ std::max(0.0, Entry.EndMicroseconds - Entry.StartMicroseconds) };
            std::string ParentName{};
            double ParentStartMicroseconds{ Entry.StartMicroseconds };
            if (Entry.Depth > 0ULL) {
                for (std::size_t ParentIndex{ EntryIndex }; ParentIndex > 0ULL; ParentIndex -= 1ULL) {
                    const ProfileEntry& ParentEntry{ Entries[ParentIndex - 1ULL] };
                    if (ParentEntry.Depth == Entry.Depth - 1ULL && ParentEntry.StartMicroseconds <= Entry.StartMicroseconds && ParentEntry.EndMicroseconds >= Entry.EndMicroseconds) {
                        ParentName = ParentEntry.Name;
                        ParentStartMicroseconds = ParentEntry.StartMicroseconds;
                        break;
                    }
                }
            }

            const double StartOffsetMicroseconds{ std::max(0.0, Entry.StartMicroseconds - ParentStartMicroseconds) };
            bool IsFound{};
            for (TimelineProfileAggregate& Aggregate : mTimelineProfileAggregates) {
                if (Aggregate.mName == Entry.Name && Aggregate.mParentName == ParentName && Aggregate.mDepth == Entry.Depth) {
                    Aggregate.mDurationMicrosecondsSum += DurationMicroseconds;
                    Aggregate.mStartOffsetMicrosecondsSum += StartOffsetMicroseconds;
                    IsFound = true;
                    break;
                }
            }

            if (!IsFound) {
                TimelineProfileAggregate Aggregate{};
                Aggregate.mName = Entry.Name;
                Aggregate.mParentName = ParentName;
                Aggregate.mDepth = Entry.Depth;
                Aggregate.mDurationMicrosecondsSum = DurationMicroseconds;
                Aggregate.mStartOffsetMicrosecondsSum = StartOffsetMicroseconds;
                mTimelineProfileAggregates.push_back(std::move(Aggregate));
            }
        }

        mTimelineProfileFrameCount += 1;

        const double ElapsedMicroseconds{ EndMicroseconds - mTimelineProfileAverageBeginMicroseconds };
        if (ElapsedMicroseconds < TimelineAverageMicroseconds) {
            return;
        }

        std::vector<ProfileEntry> AverageProfiles{};
        AverageProfiles.reserve(mTimelineProfileAggregates.size());

        const double FrameCount{ static_cast<double>(mTimelineProfileFrameCount) };
        double CurrentStartMicroseconds{};
        for (const TimelineProfileAggregate& Aggregate : mTimelineProfileAggregates) {
            ProfileEntry Entry{};
            Entry.Name = Aggregate.mName;
            Entry.Depth = Aggregate.mDepth;
            const double DurationMicroseconds{ Aggregate.mDurationMicrosecondsSum / FrameCount };
            if (Aggregate.mDepth == 0ULL) {
                Entry.StartMicroseconds = CurrentStartMicroseconds;
                Entry.EndMicroseconds = CurrentStartMicroseconds + DurationMicroseconds;
                CurrentStartMicroseconds = Entry.EndMicroseconds;
            }
            else {
                const std::vector<ProfileEntry>::reverse_iterator ParentIterator{ std::find_if(AverageProfiles.rbegin(), AverageProfiles.rend(), [&Aggregate](const ProfileEntry& ParentEntry) {
                    return ParentEntry.Name == Aggregate.mParentName && ParentEntry.Depth == Aggregate.mDepth - 1ULL;
                }) };
                if (ParentIterator == AverageProfiles.rend()) {
                    Entry.StartMicroseconds = CurrentStartMicroseconds;
                    Entry.EndMicroseconds = CurrentStartMicroseconds + DurationMicroseconds;
                }
                else {
                    const double StartOffsetMicroseconds{ Aggregate.mStartOffsetMicrosecondsSum / FrameCount };
                    Entry.StartMicroseconds = ParentIterator->StartMicroseconds + StartOffsetMicroseconds;
                    const double RequestedEndMicroseconds{ Entry.StartMicroseconds + DurationMicroseconds };
                    Entry.EndMicroseconds = std::max(Entry.StartMicroseconds, std::min(ParentIterator->EndMicroseconds, RequestedEndMicroseconds));
                }
            }
            AverageProfiles.push_back(Entry);
        }

        mTimelineAverageProfiles = std::move(AverageProfiles);
        mTimelineProfileAggregates.clear();
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
