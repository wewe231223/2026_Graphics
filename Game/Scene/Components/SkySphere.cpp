#include "SkySphere.h"
#include <format>
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* SkySphere::GetComponentInspectionName() {
        return "SkySphere";
    }

    void SkySphere::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "SkySphereEntityId", std::format("{}:{}:{}", SkySphereEntityId.index, SkySphereEntityId.generation, SkySphereEntityId.flags) });
        OutFields.push_back(ComponentInspectionField{ "SkySphereSerializedEntityId", std::format("{}", SkySphereSerializedEntityId) });
    }
}
