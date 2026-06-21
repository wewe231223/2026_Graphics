#pragma once

#include "DirectXTK12/SimpleMath.h"

namespace RenderContract {
    struct alignas(16) EnvironmentSegmentContext final {
    public:
        DirectX::SimpleMath::Matrix mLocalTransform{ DirectX::SimpleMath::Matrix::Identity };
    };
}
