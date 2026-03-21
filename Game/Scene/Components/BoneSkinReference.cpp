#include "Game/Scene/Components/BoneSkinReference.h"

#include <format>

#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* BoneSkinReference::GetComponentInspectionName() {
        return "BoneSkinReference";
    }

    void BoneSkinReference::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "BoneRootEntityId", std::format("{}:{}", boneRootEntityId.index, boneRootEntityId.generation) });
    }
}
