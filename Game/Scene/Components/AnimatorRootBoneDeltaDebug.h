#pragma once

#include "Arche/Common.h"
#include "Utility/ComponentRestraint.h"
#include "Utility/DirectXInclude.h"

namespace Game {
    Component(AnimatorRootBoneDeltaDebug)
        Arche::EntityID TrackedRootBoneEntityId{ Arche::NullEntityID };
        bool IsInitialized{ false };
        SimpleMath::Vector3 PreviousRootBoneWorldPosition{ 0.0f, 0.0f, 0.0f };
        SimpleMath::Vector3 RootBoneDeltaWorldSpace{ 0.0f, 0.0f, 0.0f };
        SimpleMath::Vector3 RootBoneDeltaTopLevelSpace{ 0.0f, 0.0f, 0.0f };
    EndComponent(AnimatorRootBoneDeltaDebug)
}
