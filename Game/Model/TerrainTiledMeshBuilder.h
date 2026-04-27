#pragma once

#include <cstdint>
#include <vector>

#include "Asset/ModelResult.h"
#include "Game/Model/TerrainMeshTypes.h"
#include "Game/Model/TerrainRenderResource.h"

namespace Game {
    struct TerrainTiledMeshData final {
    public:
        asset::ModelResult mModelData{};
        std::vector<TerrainTileMetadata> mTileMetadata{};
        std::uint32_t mTileQuadCount{ 0 };
        std::uint32_t mTileCountX{ 0 };
        std::uint32_t mTileCountZ{ 0 };
        std::uint32_t mLodCount{ 1 };
        std::vector<float> mLodDistances{};
        DirectX::BoundingOrientedBox mLocalBoundingBox{};
    };

    class TerrainTiledMeshBuilder final {
    public:
        TerrainTiledMeshBuilder();
        ~TerrainTiledMeshBuilder();
        TerrainTiledMeshBuilder(const TerrainTiledMeshBuilder& Other);
        TerrainTiledMeshBuilder& operator=(const TerrainTiledMeshBuilder& Other);
        TerrainTiledMeshBuilder(TerrainTiledMeshBuilder&& Other) noexcept;
        TerrainTiledMeshBuilder& operator=(TerrainTiledMeshBuilder&& Other) noexcept;

    public:
        TerrainTiledMeshData Build(const HeightFieldData& Field, const TerrainBuildDesc& Desc) const;

    private:
        void ValidateBuildInput(const HeightFieldData& Field, const TerrainBuildDesc& Desc) const;
        std::uint32_t CalculateIndex(std::uint32_t Width, std::uint32_t X, std::uint32_t Z) const;
        std::uint32_t CalculateLodStride(std::uint32_t LodIndex) const;
        float CalculateWorldHeight(const HeightFieldData& Field, const TerrainBuildDesc& Desc, std::uint32_t X, std::uint32_t Z) const;
        asset::Vec3 CalculatePosition(const HeightFieldData& Field, const TerrainBuildDesc& Desc, std::uint32_t X, std::uint32_t Z) const;
        asset::Vec2 CalculateTexCoord(const HeightFieldData& Field, const TerrainBuildDesc& Desc, std::uint32_t X, std::uint32_t Z) const;
        void AppendTileLodIndices(std::vector<std::uint32_t>& OutIndices, std::uint32_t FieldWidth, std::uint32_t StartX, std::uint32_t StartZ, std::uint32_t EndX, std::uint32_t EndZ, std::uint32_t LodStride) const;
        DirectX::BoundingOrientedBox BuildBoundingBoxFromRange(const asset::VertexAttributes& Vertices, std::uint32_t FieldWidth, std::uint32_t StartX, std::uint32_t StartZ, std::uint32_t EndX, std::uint32_t EndZ) const;
        DirectX::BoundingOrientedBox BuildBoundingBoxFromAllVertices(const asset::VertexAttributes& Vertices) const;
        void AccumulateNormals(asset::VertexAttributes& Vertices, const std::vector<std::uint32_t>& Indices) const;
    };
}
