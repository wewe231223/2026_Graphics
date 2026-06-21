#pragma once

#include "DirectXTK12/SimpleMath.h"

namespace RenderContract {
    struct alignas(16) EnvironmentInstanceContext final {
    public:
        DirectX::SimpleMath::Vector4 mPositionScale{};
        DirectX::SimpleMath::Vector4 mRotationVariation{};
    };
}
