#pragma once

#include "Game/Model/TerrainMeshTypes.h"

namespace Game {
    class TerrainHeightFieldFactory final {
    public:
        TerrainHeightFieldFactory();
        ~TerrainHeightFieldFactory();
        TerrainHeightFieldFactory(const TerrainHeightFieldFactory& Other);
        TerrainHeightFieldFactory& operator=(const TerrainHeightFieldFactory& Other);
        TerrainHeightFieldFactory(TerrainHeightFieldFactory&& Other) noexcept;
        TerrainHeightFieldFactory& operator=(TerrainHeightFieldFactory&& Other) noexcept;

    public:
        TerrainProceduralHeightFieldDesc ResolveProceduralHeightFieldDesc(const TerrainBuildDesc& Desc) const;
        HeightFieldData Build(const TerrainBuildDesc& Desc) const;
    };
}
