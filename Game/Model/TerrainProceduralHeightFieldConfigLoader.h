#pragma once

#include <string>

#include "Game/Model/TerrainMeshTypes.h"

namespace Game {
    class TerrainProceduralHeightFieldConfigLoader final {
    public:
        TerrainProceduralHeightFieldConfigLoader();
        ~TerrainProceduralHeightFieldConfigLoader();
        TerrainProceduralHeightFieldConfigLoader(const TerrainProceduralHeightFieldConfigLoader& Other);
        TerrainProceduralHeightFieldConfigLoader& operator=(const TerrainProceduralHeightFieldConfigLoader& Other);
        TerrainProceduralHeightFieldConfigLoader(TerrainProceduralHeightFieldConfigLoader&& Other) noexcept;
        TerrainProceduralHeightFieldConfigLoader& operator=(TerrainProceduralHeightFieldConfigLoader&& Other) noexcept;

    public:
        TerrainProceduralHeightFieldDesc Load(const std::string& Path) const;
    };
}
