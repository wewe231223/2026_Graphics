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
        OutFields.push_back(ComponentInspectionField{ "HasRootBonePosition", hasRootBonePosition == true ? "true" : "false" });

        if (hasRootBonePosition == true) {
            OutFields.push_back(ComponentInspectionField{ "RootBonePosition", FormatVector3(rootBonePosition) });
        }

        if (hasPreviousRootBonePosition == true) {
            OutFields.push_back(ComponentInspectionField{ "PreviousRootBonePosition", FormatVector3(previousRootBonePosition) });
        }

        OutFields.push_back(ComponentInspectionField{ "HasRootBoneWorldDelta", hasRootBoneWorldDelta == true ? "true" : "false" });

        if (hasRootBoneWorldDelta == true) {
            const SimpleMath::Vector3 Delta{ rootBoneWorldDelta };
            OutFields.push_back(ComponentInspectionField{ "RootBoneDelta", FormatVector3(Delta) });
        }
    }
}
