#pragma once

#include <cstdint>

#include "DirectXTK12/SimpleMath.h"

namespace RenderContract {
    constexpr std::uint32_t FrameGlobalFlagDrawBoundingBoxes{ 0x1u };
    constexpr std::uint32_t FrameGlobalFlagDrawDebugGeometry{ 0x2u };

    struct alignas(16) FrameGlobals final {
    public:
        DirectX::SimpleMath::Matrix mView{};
        DirectX::SimpleMath::Matrix mProj{};
        DirectX::SimpleMath::Matrix mViewProj{};
        DirectX::SimpleMath::Matrix mPrevView{};
        DirectX::SimpleMath::Matrix mPrevProj{};
        DirectX::SimpleMath::Matrix mPrevViewProj{};
        DirectX::SimpleMath::Vector4 mRenderTargetSize{};
        float mDt{};
        std::uint32_t mFrameIndex{};
        std::uint32_t mFlags{};
        std::uint32_t mPadding0{};
    };
}
