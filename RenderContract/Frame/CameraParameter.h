#pragma once

#include "DirectXTK12/SimpleMath.h"

namespace RenderContract {
    struct alignas(16) CameraParameter final {
    public:
        DirectX::SimpleMath::Matrix mView{};
        DirectX::SimpleMath::Matrix mProj{};
        DirectX::SimpleMath::Matrix mViewProj{};
        DirectX::SimpleMath::Vector4 mPosition{};
        float mNearPlane{};
        float mFarPlane{};
        float mAspectRatio{};
        float mFovRadians{};
    };
}
