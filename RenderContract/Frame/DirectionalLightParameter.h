#pragma once

#include <cstdint>

#include "DirectXTK12/SimpleMath.h"

namespace RenderContract {
    constexpr std::uint32_t DirectionalLightParameterFlagActive{ 0x1u };
    constexpr std::uint32_t DirectionalLightParameterFlagCastShadow{ 0x2u };

    struct alignas(16) DirectionalLightParameter final {
    public:
        DirectX::SimpleMath::Vector4 mDirection{ 0.0f, -1.0f, 0.0f, 0.0f };
        DirectX::SimpleMath::Vector4 mColor{ 1.0f, 0.97f, 0.92f, 1.0f };
        float mIntensity{ 1.2f };
        float mAmbientIntensity{ 0.25f };
        std::uint32_t mFlags{ DirectionalLightParameterFlagActive | DirectionalLightParameterFlagCastShadow };
        float mPadding0{};
    };

    static_assert(sizeof(DirectionalLightParameter) == 48U);
}
