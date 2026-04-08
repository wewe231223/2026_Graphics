#include "Game/Scene/Components/TerrainCollider.h"

#include <format>
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* TerrainCollider::GetComponentInspectionName() {
        return "TerrainCollider";
    }

    void TerrainCollider::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "TerrainCollide", std::format("{}", mTerrainCollide) });
    }
}
