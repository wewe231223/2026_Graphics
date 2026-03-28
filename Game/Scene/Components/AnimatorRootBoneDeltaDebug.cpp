#include "AnimatorRootBoneDeltaDebug.h"

#include <format>

#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* AnimatorRootBoneDeltaDebug::GetComponentInspectionName() {
        return "AnimatorRootBoneDeltaDebug";
    }

    void AnimatorRootBoneDeltaDebug::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "TrackedRootBoneEntityId", std::format("{}:{}", TrackedRootBoneEntityId.index, TrackedRootBoneEntityId.generation) });
        OutFields.push_back(ComponentInspectionField{ "IsInitialized", IsInitialized == true ? "true" : "false" });
        OutFields.push_back(ComponentInspectionField{ "RootBoneDeltaWorldSpace", std::format("{:.6f}, {:.6f}, {:.6f}", RootBoneDeltaWorldSpace.x, RootBoneDeltaWorldSpace.y, RootBoneDeltaWorldSpace.z) });
        OutFields.push_back(ComponentInspectionField{ "RootBoneDeltaTopLevelSpace", std::format("{:.6f}, {:.6f}, {:.6f}", RootBoneDeltaTopLevelSpace.x, RootBoneDeltaTopLevelSpace.y, RootBoneDeltaTopLevelSpace.z) });
    }
}
