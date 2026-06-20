#pragma once

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <string>
#include <vector>
#include "Utility/DirectXInclude.h"

namespace Widget {
    struct FrameTimeSample final {
        float mAgeSeconds{};
        float mCpuTimeMilliseconds{};
        float mGpuTimeMilliseconds{};
        bool mHasGpuTime{};
    };

    struct ProfileEntry {
        std::string Name{};
        double StartMicroseconds{};
        double EndMicroseconds{};
        std::size_t Depth{};
    };

    class PerformanceProvider {
    private:
        struct FrameTimeRecord final {
            std::uint64_t mFrameIdentifier{};
            double mEndMicroseconds{};
            float mCpuTimeMicroseconds{};
            float mGpuTimeMicroseconds{};
            bool mHasGpuTime{};
        };

    public:
        PerformanceProvider();
        ~PerformanceProvider();
        PerformanceProvider(const PerformanceProvider& Other) = delete;
        PerformanceProvider& operator=(const PerformanceProvider& Other) = delete;
        PerformanceProvider(PerformanceProvider&& Other) = delete;
        PerformanceProvider& operator=(PerformanceProvider&& Other) = delete;

    public:
        static PerformanceProvider& Get();

        void Initialize(IDXGIAdapter1* Adapter);

        void BeginFrame();
        void EndFrame();
        void SubmitGpuFrameTime(std::uint64_t FrameIdentifier, double GpuTimeMicroseconds);

        void BeginPhaseProfile(const std::string& Name);
        void EndPhaseProfile();

        [[nodiscard]] std::uint64_t GetCurrentFrameIdentifier() const;
        [[nodiscard]] std::vector<float> GetCpuFrameTimeMilliseconds() const;
        [[nodiscard]] std::vector<float> GetGpuFrameTimeMilliseconds() const;
        [[nodiscard]] std::vector<FrameTimeSample> GetFrameTimeSamples() const;
        [[nodiscard]] float GetFrameTimeHistorySeconds() const;
        [[nodiscard]] double GetAverageFps() const;
        [[nodiscard]] double GetOnePercentLowFps() const;
        [[nodiscard]] double GetZeroPointOnePercentLowFps() const;

        [[nodiscard]] std::vector<ProfileEntry> GetCurrentFrameProfiles() const;
        [[nodiscard]] std::vector<ProfileEntry> GetTimelineAverageProfiles() const;

        [[nodiscard]] uint64_t GetVramBudgetBytes() const;
        [[nodiscard]] uint64_t GetVramUsageBytes() const;
        [[nodiscard]] float GetVramUsageRatio() const;

    private:
        void PruneOldFrameTimeRecords(double NowMicroseconds);
        void UpdateTimelineAverageProfiles(const std::vector<ProfileEntry>& Entries, double EndMicroseconds);
        void UpdatePercentileCache();
        void UpdateVramInfoIfNeeded();
        double QueryNowMicroseconds() const;

    private:
        LARGE_INTEGER mFrequency{};
        bool mHasFrameBegin{};
        double mFrameBeginMicroseconds{};
        std::uint64_t mCurrentFrameIdentifier{};

        std::vector<FrameTimeRecord> mFrameTimeRecords{};

        IDXGIAdapter3* mAdapter3{};
        double mLastVramUpdateMicroseconds{};
        uint64_t mVramBudgetBytes{};
        uint64_t mVramUsageBytes{};

        std::vector<std::pair<std::string, double>> mPhaseDurations{};
        std::string mActivePhaseName{};
        double mActivePhaseStartMicroseconds{};
        bool mHasActivePhase{};
        std::vector<ProfileEntry> mCurrentFrameProfiles{};
        std::vector<ProfileEntry> mTimelineAverageProfiles{};
        std::vector<std::pair<std::string, double>> mTimelineProfileDurationSums{};
        std::size_t mTimelineProfileFrameCount{};
        double mTimelineProfileAverageBeginMicroseconds{};

        double mLastPercentileUpdateMicroseconds{};
        double mAverageFps{};
        double mOnePercentLowFps{};
        double mZeroPointOnePercentLowFps{};
    };
}
