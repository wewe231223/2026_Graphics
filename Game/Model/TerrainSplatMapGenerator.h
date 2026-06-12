#pragma once

#include "Game/Model/TerrainMeshTypes.h"

namespace Game {
    class TerrainSplatMapGenerator final {
    public:
        TerrainSplatMapGenerator();
        ~TerrainSplatMapGenerator();
        TerrainSplatMapGenerator(const TerrainSplatMapGenerator& Other);
        TerrainSplatMapGenerator& operator=(const TerrainSplatMapGenerator& Other);
        TerrainSplatMapGenerator(TerrainSplatMapGenerator&& Other) noexcept;
        TerrainSplatMapGenerator& operator=(TerrainSplatMapGenerator&& Other) noexcept;

    public:
        SplatMapData Generate(const HeightFieldData& Field, const TerrainBuildDesc& Desc) const;
    };
}
