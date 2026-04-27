#include "TerrainRenderer.h"
#include <format>
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* TerrainRenderer::GetComponentInspectionName() {
        return "TerrainRenderer";
    }

    void TerrainRenderer::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "ResourceBound", mResource == nullptr ? "false" : "true" });
        OutFields.push_back(ComponentInspectionField{ "TileQuadCount", std::format("{}", mTileQuadCount) });
        const std::string TileMetadataIndexText{ mTileMetadataIndex == InvalidTerrainTileMetadataIndex ? std::string{ "Parent" } : std::format("{}", mTileMetadataIndex) };
        OutFields.push_back(ComponentInspectionField{ "TileMetadataIndex", TileMetadataIndexText });
        OutFields.push_back(ComponentInspectionField{ "Active", mActive ? "true" : "false" });
    }
}
