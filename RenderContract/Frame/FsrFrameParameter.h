#pragma once

#include <cstdint>

#include "DirectXTK12/SimpleMath.h"

namespace RenderContract {
    struct FsrFrameParameter final {
    public:
        bool mEnabled{};
        bool mJitterEnabled{};
        bool mResetHistory{};
        std::uint32_t mRenderWidth{};
        std::uint32_t mRenderHeight{};
        std::uint32_t mDisplayWidth{};
        std::uint32_t mDisplayHeight{};
        std::int32_t mJitterPhaseCount{};
        std::int32_t mJitterIndex{};
        DirectX::SimpleMath::Vector2 mJitterOffset{};
        DirectX::SimpleMath::Vector2 mJitterOffsetNdc{};
        DirectX::SimpleMath::Vector2 mMotionVectorScale{};
    };
}
