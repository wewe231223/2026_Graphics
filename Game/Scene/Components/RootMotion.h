#pragma once

#include "DirectXTK12/SimpleMath.h"
#include "Utility/ComponentRestraint.h"

namespace SimpleMath = DirectX::SimpleMath;

namespace Game {
    Component(RootMotion)
        SimpleMath::Vector3 rootBoneWorldPosition{};
        SimpleMath::Vector3 previousRootBoneWorldPosition{};
        bool hasRootBoneWorldPosition{ false };
        bool hasPreviousRootBoneWorldPosition{ false };
    EndComponent(RootMotion)
}
