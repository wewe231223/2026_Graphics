#pragma once

#include <cstdint>
#include <filesystem>

#include "DirectXTK12/SimpleMath.h"

namespace Core {
    namespace DX {
        enum class FsrQualityMode : std::uint32_t {
            NativeAA,
            Quality,
            Balanced,
            Performance,
            UltraPerformance,
            Custom
        };

        struct FsrParameter final {
        public:
            bool mEnabled{};
            bool mJitterEnabled{ true };
            bool mSharpeningEnabled{ true };
            bool mHighDynamicRange{ true };
            bool mAutoExposureEnabled{ true };
            bool mDepthInverted{};
            bool mDepthInfinite{};
            bool mDebugChecking{};
            bool mDebugVisualization{};
            bool mMotionVectorsJitterCancellation{ true };
            bool mResetHistory{};
            FsrQualityMode mQualityMode{ FsrQualityMode::Quality };
            float mSharpness{ 0.35f };
            float mCustomUpscaleRatio{ 1.5f };
            float mRenderScaleOverride{};
        };

        struct FsrJitterSample final {
        public:
            DirectX::SimpleMath::Vector2 mPixelOffset{};
            DirectX::SimpleMath::Vector2 mNdcOffset{};
            std::int32_t mPhaseCount{};
            std::int32_t mIndex{};
        };

        class FsrParameterFile final {
        public:
            FsrParameterFile() = default;
            ~FsrParameterFile() = default;

            FsrParameterFile(const FsrParameterFile& Other) = delete;
            FsrParameterFile& operator=(const FsrParameterFile& Other) = delete;

            FsrParameterFile(FsrParameterFile&& Other) = delete;
            FsrParameterFile& operator=(FsrParameterFile&& Other) = delete;

        public:
            bool Read(const std::filesystem::path& Path, FsrParameter& OutParameter) const;
        };

        float ResolveFsrUpscaleRatio(FsrQualityMode QualityMode, float CustomUpscaleRatio);
        std::uint32_t ResolveFsrRenderSize(std::uint32_t DisplaySize, float UpscaleRatio);
        std::int32_t ResolveFsrJitterPhaseCount(std::uint32_t RenderWidth, std::uint32_t DisplayWidth);
        FsrJitterSample BuildFsrJitterSample(std::uint32_t JitterIndex, std::uint32_t RenderWidth, std::uint32_t RenderHeight, std::uint32_t DisplayWidth);
    }
}
