#pragma once

#include "DirectXTK12/SimpleMath.h"
#include "Utility/ComponentRestraint.h"

namespace SimpleMath = DirectX::SimpleMath;

namespace Game {
    Component(RootMotion)
        SimpleMath::Vector3 rootBonePosition{};
        SimpleMath::Vector3 previousRootBonePosition{};
        SimpleMath::Vector3 rootBoneWorldDelta{};
        bool hasRootBonePosition{ false };
        bool hasPreviousRootBonePosition{ false };
        bool hasRootBoneWorldDelta{ false };
    EndComponent(RootMotion)
}
