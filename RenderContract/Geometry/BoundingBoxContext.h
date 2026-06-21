#pragma once

#include "DirectXTK12/SimpleMath.h"

namespace RenderContract {
    struct alignas(16) BoundingBoxContext final {
    public:
        DirectX::SimpleMath::Vector4 mCenter{};
        DirectX::SimpleMath::Vector4 mExtents{};
        DirectX::SimpleMath::Vector4 mOrientation{};
    };
}
