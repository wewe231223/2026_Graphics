#include "Game/Scene/Components/TerrainCollidee.h"

#include <cstdint>
#include <format>
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* TerrainCollidee::GetComponentInspectionName() {
        return "TerrainCollidee";
    }

    void TerrainCollidee::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "TerrainHeightResolver", std::format("{}", reinterpret_cast<std::uintptr_t>(mTerrainHeightResolver)) });
    }
}
