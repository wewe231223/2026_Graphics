#include "TerrainRenderer.h"
#include <format>
#include <sstream>
#include "Game/Scene/Components/ComponentInspection.h"

namespace {
    std::string BuildLodDistanceText(const std::vector<float>& LodDistances) {
        if (LodDistances.empty() == true) {
            return "[]";
        }

        std::ostringstream Stream{};
        Stream << "[";
        for (std::size_t Index{ 0 }; Index < LodDistances.size(); ++Index) {
            if (Index > 0ULL) {
                Stream << ", ";
            }

            Stream << LodDistances[Index];
        }

        Stream << "]";
        return Stream.str();
    }
}

namespace Game {
    const char* TerrainRenderer::GetComponentInspectionName() {
        return "TerrainRenderer";
    }

    void TerrainRenderer::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "ResourceBound", mResource == nullptr ? "false" : "true" });
        OutFields.push_back(ComponentInspectionField{ "TileQuadCount", std::format("{}", mTileQuadCount) });
        OutFields.push_back(ComponentInspectionField{ "LodCount", mResource == nullptr ? "0" : std::format("{}", mResource->GetLodCount()) });
        OutFields.push_back(ComponentInspectionField{ "LodDistances", mResource == nullptr ? "[]" : BuildLodDistanceText(mResource->GetLodDistances()) });
        const std::string TileMetadataIndexText{ mTileMetadataIndex == InvalidTerrainTileMetadataIndex ? std::string{ "Parent" } : std::format("{}", mTileMetadataIndex) };
        OutFields.push_back(ComponentInspectionField{ "TileMetadataIndex", TileMetadataIndexText });
        OutFields.push_back(ComponentInspectionField{ "Active", mActive ? "true" : "false" });
    }
}
