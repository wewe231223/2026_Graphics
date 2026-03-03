#pragma once

#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "Utility/DirectXInclude.h"

namespace Widget {
    template<typename T, std::size_t Capacity>
    class RingBuffer {
    public:
        RingBuffer() = default;
        ~RingBuffer() = default;
        RingBuffer(const RingBuffer& Other) = default;
        RingBuffer& operator=(const RingBuffer& Other) = default;
        RingBuffer(RingBuffer&& Other) noexcept = default;
        RingBuffer& operator=(RingBuffer&& Other) noexcept = default;

    public:
        void PushBack(const T& Value) {
            mData[mHead] = Value;
            mHead = (mHead + 1) % Capacity;
            if (mSize < Capacity) {
                mSize += 1;
            }
        }

        [[nodiscard]] std::size_t Size() const {
            return mSize;
        }

        [[nodiscard]] const T& At(std::size_t Index) const {
            const std::size_t StartIndex{ (mHead + Capacity - mSize) % Capacity };
            const std::size_t DataIndex{ (StartIndex + Index) % Capacity };
            return mData[DataIndex];
        }

        [[nodiscard]] std::vector<T> ToVector() const {
            std::vector<T> Values{};
            Values.reserve(mSize);
            for (std::size_t Index{ 0 }; Index < mSize; ++Index) {
                Values.push_back(At(Index));
            }
            return Values;
        }

    private:
        std::array<T, Capacity> mData{};
        std::size_t mHead{};
        std::size_t mSize{};
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

        void BeginProfile(const std::string& Name);
        void EndProfile();

        [[nodiscard]] std::vector<float> GetFrameTimeMilliseconds() const;
        [[nodiscard]] double GetAverageFps() const;
        [[nodiscard]] double GetOnePercentLowFps() const;
        [[nodiscard]] double GetZeroPointOnePercentLowFps() const;

        [[nodiscard]] std::vector<ProfileEntry> GetCurrentFrameProfiles() const;

        [[nodiscard]] uint64_t GetVramBudgetBytes() const;
        [[nodiscard]] uint64_t GetVramUsageBytes() const;
        [[nodiscard]] float GetVramUsageRatio() const;

    private:
        void UpdatePercentileCache();
        void UpdateVramInfoIfNeeded();
        double QueryNowMicroseconds() const;

    private:
        struct ActiveProfile {
            std::string Name{};
            double StartMicroseconds{};
        };

    private:
        LARGE_INTEGER mFrequency{};
        LARGE_INTEGER mFrameBeginCounter{};
        bool mHasFrameBegin{};

        RingBuffer<float, 500> mFrameTimeMicroseconds{};

        IDXGIAdapter3* mAdapter3{};
        double mLastVramUpdateMicroseconds{};
        uint64_t mVramBudgetBytes{};
        uint64_t mVramUsageBytes{};

        std::vector<ActiveProfile> mActiveProfiles{};
        std::vector<ProfileEntry> mCurrentFrameProfiles{};

        double mLastPercentileUpdateMicroseconds{};
        double mAverageFps{};
        double mOnePercentLowFps{};
        double mZeroPointOnePercentLowFps{};
    };
}
