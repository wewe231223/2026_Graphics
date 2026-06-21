#pragma once

#include <cstdint>

#include "DirectXTK12/SimpleMath.h"

namespace RenderContract {
    struct alignas(16) ModelContext final {
    public:
        DirectX::SimpleMath::Matrix mWorld{};
        DirectX::SimpleMath::Matrix mPrevWorld{};
        std::uint32_t mFlags{};
        std::uint32_t mBoneIndexStart{};
        std::uint32_t mObjectId{};
        std::uint32_t mPadding0{};
        DirectX::SimpleMath::Vector4 mCustom0{};
        DirectX::SimpleMath::Vector4 mCustom1{};
    };
}
