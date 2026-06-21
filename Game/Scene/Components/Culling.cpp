#include "Game/Scene/Components/Culling.h"
#include <format>
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* Culling::GetComponentInspectionName() {
        return "Culling";
    }

    void Culling::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "FrustumCulling", std::format("{}", frustumCulling) });
    }
}
