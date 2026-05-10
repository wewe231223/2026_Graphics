#pragma once

#include "Game/Model/TerrainMeshTypes.h"

namespace Game {
    class TerrainMeshBuilder final {
    public:
        TerrainMeshBuilder();
        ~TerrainMeshBuilder();
        TerrainMeshBuilder(const TerrainMeshBuilder& Other);
        TerrainMeshBuilder& operator=(const TerrainMeshBuilder& Other);
        TerrainMeshBuilder(TerrainMeshBuilder&& Other) noexcept;
        TerrainMeshBuilder& operator=(TerrainMeshBuilder&& Other) noexcept;

    public:
        TerrainMeshData Build(const HeightFieldData& Field, const TerrainBuildDesc& Desc) const;

    private:
        void ValidateBuildInput(const HeightFieldData& Field, const TerrainBuildDesc& Desc) const;
        std::uint32_t CalculateIndex(std::uint32_t Width, std::uint32_t X, std::uint32_t Y) const;
        float CalculateWorldHeight(const HeightFieldData& Field, const TerrainBuildDesc& Desc, std::uint32_t X, std::uint32_t Y) const;
        SimpleMath::Vector3 CalculateNormal(const HeightFieldData& Field, const TerrainBuildDesc& Desc, std::uint32_t X, std::uint32_t Y) const;
        std::uint32_t ClampCoordinate(std::int32_t Value, std::uint32_t MaxValue) const;
    };
}
