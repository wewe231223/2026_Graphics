#pragma once

#include <string>

#include "Terrain/TerrainProceduralHeightFieldConfigLoader.h"
#include "Terrain/TerrainMeshTypes.h"

namespace Terrain {
    class TerrainProceduralHeightFieldGenerator final {
    public:
        TerrainProceduralHeightFieldGenerator();
        ~TerrainProceduralHeightFieldGenerator();
        TerrainProceduralHeightFieldGenerator(const TerrainProceduralHeightFieldGenerator& Other);
        TerrainProceduralHeightFieldGenerator& operator=(const TerrainProceduralHeightFieldGenerator& Other);
        TerrainProceduralHeightFieldGenerator(TerrainProceduralHeightFieldGenerator&& Other) noexcept;
        TerrainProceduralHeightFieldGenerator& operator=(TerrainProceduralHeightFieldGenerator&& Other) noexcept;

    public:
        HeightFieldData GenerateFromConfig(const std::string& ConfigPath) const;
        HeightFieldData Generate(const TerrainProceduralHeightFieldDesc& Desc) const;

    private:
        TerrainProceduralHeightFieldConfigLoader mConfigLoader{};
    };
}
