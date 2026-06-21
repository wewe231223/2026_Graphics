#pragma once

#include <cstdint>

#include "DirectXTK12/SimpleMath.h"

namespace RenderContract {
    struct alignas(16) MaterialFieldGpu final {
    public:
        std::uint32_t mType{};
        std::uint32_t mPadding0{};
        std::uint32_t mPadding1{};
        std::uint32_t mPadding2{};
        DirectX::SimpleMath::Vector4 mFloatValue{};
        std::int64_t mIntValue{ -1 };
        std::uint64_t mPadding3{};
    };
}
