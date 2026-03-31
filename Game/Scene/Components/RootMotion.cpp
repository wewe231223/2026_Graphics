#include "RootMotion.h"

#include <format>
#include <string>

#include "Game/Scene/Components/ComponentInspection.h"

namespace {
    std::string FormatVector3(const SimpleMath::Vector3& Value) {
        return std::format("{:.3f}, {:.3f}, {:.3f}", Value.x, Value.y, Value.z);
    }
}

namespace Game {
    const char* RootMotion::GetComponentInspectionName() {
        return "RootMotion";
    }

    void RootMotion::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "RootBoneWorldPosition", hasRootBoneWorldPosition == true ? "true" : "false" });

        if (hasRootBoneWorldPosition == true) {
            OutFields.push_back(ComponentInspectionField{ "RootBonePosition", FormatVector3(rootBoneWorldPosition) });
        }

        if (hasRootBoneWorldPosition == true && hasPreviousRootBoneWorldPosition == true) {
            const SimpleMath::Vector3 Delta{ rootBoneWorldPosition - previousRootBoneWorldPosition };
            OutFields.push_back(ComponentInspectionField{ "RootBoneDelta", FormatVector3(Delta) });
        }
    }
}
