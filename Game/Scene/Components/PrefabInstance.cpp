#include "PrefabInstance.h"
#include <format>
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* PrefabInstance::GetComponentInspectionName() {
        return "PrefabInstance";
    }

    void PrefabInstance::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "PrefabId", std::format("{}", PrefabId) });
    }
}
