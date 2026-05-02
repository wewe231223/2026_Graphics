#pragma once

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <string>
#include <vector>
#include "Utility/DirectXInclude.h"

namespace Widget {
    struct FrameTimeSample {
        float AgeSeconds{};
        float TimeMilliseconds{};
    };

    struct ProfileEntry {
        std::string Name{};
        double StartMicroseconds{};
        double EndMicroseconds{};
        std::size_t Depth{};
    };

    class PerformanceProvider {
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

        void BeginPhaseProfile(const std::string& Name);
        void EndPhaseProfile();

        [[nodiscard]] std::vector<float> GetFrameTimeMilliseconds() const;
        [[nodiscard]] std::vector<FrameTimeSample> GetFrameTimeSamples() const;
        [[nodiscard]] float GetFrameTimeHistorySeconds() const;
        [[nodiscard]] double GetAverageFps() const;
        [[nodiscard]] double GetOnePercentLowFps() const;
        [[nodiscard]] double GetZeroPointOnePercentLowFps() const;

        [[nodiscard]] std::vector<ProfileEntry> GetCurrentFrameProfiles() const;

        [[nodiscard]] uint64_t GetVramBudgetBytes() const;
        [[nodiscard]] uint64_t GetVramUsageBytes() const;
        [[nodiscard]] float GetVramUsageRatio() const;

    private:
        void PruneOldFrameTimeRecords(double NowMicroseconds);
        void UpdatePercentileCache();
        void UpdateVramInfoIfNeeded();
        double QueryNowMicroseconds() const;

    private:
        LARGE_INTEGER mFrequency{};
        LARGE_INTEGER mFrameBeginCounter{};
        bool mHasFrameBegin{};
        double mFrameBeginMicroseconds{};

        std::vector<std::pair<double, float>> mFrameTimeRecords{};

        IDXGIAdapter3* mAdapter3{};
        double mLastVramUpdateMicroseconds{};
        uint64_t mVramBudgetBytes{};
        uint64_t mVramUsageBytes{};

        std::vector<std::pair<std::string, double>> mPhaseDurations{};
        std::string mActivePhaseName{};
        double mActivePhaseStartMicroseconds{};
        bool mHasActivePhase{};
        std::vector<ProfileEntry> mCurrentFrameProfiles{};

        double mLastPercentileUpdateMicroseconds{};
        double mAverageFps{};
        double mOnePercentLowFps{};
        double mZeroPointOnePercentLowFps{};
    };
}
