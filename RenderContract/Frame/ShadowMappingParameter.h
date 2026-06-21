#pragma once

#include <array>
#include <cstdint>

#include <DirectXCollision.h>
#include "DirectXTK12/SimpleMath.h"

#include "RenderContract/Frame/CameraParameter.h"
#include "RenderContract/Frame/DirectionalLightParameter.h"

namespace RenderContract {
    constexpr std::uint32_t ShadowCascadeMaxCount{ 4u };

    struct alignas(16) ShadowMappingParameter final {
    public:
        CameraParameter mShadowCameras[ShadowCascadeMaxCount]{};
        DirectionalLightParameter mDirectionalLight{};
        DirectX::SimpleMath::Vector4 mCascadeSplitDistances{};
        float mShadowBiases[ShadowCascadeMaxCount]{};
        float mShadowStrengths[ShadowCascadeMaxCount]{};
        float mShadowMapSizes[ShadowCascadeMaxCount]{};
        float mRasterDepthBiases[ShadowCascadeMaxCount]{};
        float mRasterSlopeScaledDepthBiases[ShadowCascadeMaxCount]{};
        std::uint32_t mCascadeCount{};
        float mMinimumShadowMapSize{};
        float mMinimumProjectionDivisor{};
        float mMinimumProjectionDepthSpan{};
    };

    static_assert(sizeof(ShadowMappingParameter) == 1056U);

    std::uint32_t ResolveShadowCascadeCount(const ShadowMappingParameter& ShadowMappingParameterValue);
    std::array<DirectX::BoundingOrientedBox, ShadowCascadeMaxCount> BuildShadowCullingBoxes(const ShadowMappingParameter& ShadowMappingParameterValue);
}
