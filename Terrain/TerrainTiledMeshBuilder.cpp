#include "TerrainTiledMeshBuilder.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
    DirectX::BoundingOrientedBox BuildObbFromMinMax(const asset::Vec3& MinValue, const asset::Vec3& MaxValue) {
        DirectX::BoundingOrientedBox Obb{};
        Obb.Center = DirectX::XMFLOAT3{ (MinValue.x + MaxValue.x) * 0.5f, (MinValue.y + MaxValue.y) * 0.5f, (MinValue.z + MaxValue.z) * 0.5f };
        Obb.Extents = DirectX::XMFLOAT3{ (MaxValue.x - MinValue.x) * 0.5f, (MaxValue.y - MinValue.y) * 0.5f, (MaxValue.z - MinValue.z) * 0.5f };
        Obb.Orientation = DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f };
        return Obb;
    }
}

namespace Terrain {
    TerrainTiledMeshBuilder::TerrainTiledMeshBuilder() {
    }

    TerrainTiledMeshBuilder::~TerrainTiledMeshBuilder() {
    }

    TerrainTiledMeshBuilder::TerrainTiledMeshBuilder(const TerrainTiledMeshBuilder& Other) {
        (void)Other;
    }

    TerrainTiledMeshBuilder& TerrainTiledMeshBuilder::operator=(const TerrainTiledMeshBuilder& Other) {
        (void)Other;
        return *this;
    }

    TerrainTiledMeshBuilder::TerrainTiledMeshBuilder(TerrainTiledMeshBuilder&& Other) noexcept {
        (void)Other;
    }

    TerrainTiledMeshBuilder& TerrainTiledMeshBuilder::operator=(TerrainTiledMeshBuilder&& Other) noexcept {
        (void)Other;
        return *this;
    }

    TerrainTiledMeshData TerrainTiledMeshBuilder::Build(const HeightFieldData& Field, const TerrainBuildDesc& Desc) const {
        ValidateBuildInput(Field, Desc);

        TerrainTiledMeshData TiledData{};
        TiledData.mTileQuadCount = Desc.TileQuadCount;
        TiledData.mLodCount = Desc.LodCount;
        TiledData.mLodDistances = Desc.LodDistances;

        const std::uint32_t QuadCountX{ Field.Width - 1 };
        const std::uint32_t QuadCountZ{ Field.Height - 1 };

        TiledData.mTileCountX = (QuadCountX + Desc.TileQuadCount - 1u) / Desc.TileQuadCount;
        TiledData.mTileCountZ = (QuadCountZ + Desc.TileQuadCount - 1u) / Desc.TileQuadCount;
        TiledData.mTileMetadata.reserve(static_cast<std::size_t>(TiledData.mTileCountX) * static_cast<std::size_t>(TiledData.mTileCountZ));

        asset::ModelNode& RootNode{ TiledData.mModelData.CreateNode("Terrain", nullptr) };
        const std::size_t PatchCount{ static_cast<std::size_t>(TiledData.mTileCountX) * static_cast<std::size_t>(TiledData.mTileCountZ) };
        RootNode.Vertices().Resize(PatchCount * 4ULL);
        RootNode.Indices().reserve(PatchCount * 4ULL);

        std::uint32_t PatchVertexIndex{ 0 };
        for (std::uint32_t TileZ{ 0 }; TileZ < TiledData.mTileCountZ; ++TileZ) {
            for (std::uint32_t TileX{ 0 }; TileX < TiledData.mTileCountX; ++TileX) {
                const std::uint32_t StartX{ TileX * Desc.TileQuadCount };
                const std::uint32_t StartZ{ TileZ * Desc.TileQuadCount };
                const std::uint32_t EndX{ std::min(StartX + Desc.TileQuadCount, QuadCountX) };
                const std::uint32_t EndZ{ std::min(StartZ + Desc.TileQuadCount, QuadCountZ) };
                const std::uint32_t QuadCountInTileX{ EndX - StartX };
                const std::uint32_t QuadCountInTileZ{ EndZ - StartZ };

                TerrainTileMetadata TileMetadata{};
                TileMetadata.mTileIndexX = TileX;
                TileMetadata.mTileIndexZ = TileZ;
                TileMetadata.mStartX = StartX;
                TileMetadata.mStartZ = StartZ;
                TileMetadata.mQuadCountX = QuadCountInTileX;
                TileMetadata.mQuadCountZ = QuadCountInTileZ;
                TileMetadata.mLocalBoundingBox = BuildBoundingBoxFromRange(Field, Desc, StartX, StartZ, EndX, EndZ);
                TileMetadata.mCenter = SimpleMath::Vector3{ TileMetadata.mLocalBoundingBox.Center.x, TileMetadata.mLocalBoundingBox.Center.y, TileMetadata.mLocalBoundingBox.Center.z };

                WritePatchVertex(RootNode.Vertices(), PatchVertexIndex + 0u, Field, Desc, StartX, StartZ);
                WritePatchVertex(RootNode.Vertices(), PatchVertexIndex + 1u, Field, Desc, EndX, StartZ);
                WritePatchVertex(RootNode.Vertices(), PatchVertexIndex + 2u, Field, Desc, StartX, EndZ);
                WritePatchVertex(RootNode.Vertices(), PatchVertexIndex + 3u, Field, Desc, EndX, EndZ);

                const std::size_t IndexOffset{ RootNode.Indices().size() };
                RootNode.Indices().push_back(PatchVertexIndex + 0u);
                RootNode.Indices().push_back(PatchVertexIndex + 1u);
                RootNode.Indices().push_back(PatchVertexIndex + 2u);
                RootNode.Indices().push_back(PatchVertexIndex + 3u);

                asset::ModelNode::SubMesh SubMesh{};
                SubMesh.IndexOffset = IndexOffset;
                SubMesh.IndexCount = 4ULL;
                SubMesh.MaterialGroupItemIndex = 0;
                RootNode.SubMeshes().push_back(SubMesh);
                TileMetadata.mSubMeshIndex = static_cast<std::uint32_t>(RootNode.SubMeshes().size() - 1ULL);

                TiledData.mTileMetadata.push_back(std::move(TileMetadata));
                PatchVertexIndex += 4u;
            }
        }

        TiledData.mLocalBoundingBox = BuildBoundingBoxFromRange(Field, Desc, 0u, 0u, QuadCountX, QuadCountZ);
        RootNode.SetBoundingBox(TiledData.mLocalBoundingBox);
        return TiledData;
    }

    void TerrainTiledMeshBuilder::ValidateBuildInput(const HeightFieldData& Field, const TerrainBuildDesc& Desc) const {
        if (Desc.MaxHeight <= 0.0f) {
            throw std::runtime_error{ "MaxHeight must be greater than zero." };
        }

        if (Desc.CellSizeX <= 0.0f) {
            throw std::runtime_error{ "CellSizeX must be greater than zero." };
        }

        if (Desc.CellSizeZ <= 0.0f) {
            throw std::runtime_error{ "CellSizeZ must be greater than zero." };
        }

        if (Desc.TileQuadCount == 0u) {
            throw std::runtime_error{ "TileQuadCount must be greater than zero." };
        }

        if (Desc.TileQuadCount > 64u) {
            throw std::runtime_error{ "TileQuadCount must be 64 or less for terrain tessellation patch rendering." };
        }

        if (Desc.LodCount == 0u) {
            throw std::runtime_error{ "LodCount must be greater than zero." };
        }

        if (Field.Width < 2u || Field.Height < 2u) {
            throw std::runtime_error{ "Height field size must be at least 2x2." };
        }

        const std::size_t ExpectedSize{ static_cast<std::size_t>(Field.Width) * static_cast<std::size_t>(Field.Height) };
        if (Field.HeightValues.size() != ExpectedSize) {
            throw std::runtime_error{ "Height01 buffer size mismatch." };
        }
    }

    std::uint32_t TerrainTiledMeshBuilder::CalculateIndex(std::uint32_t Width, std::uint32_t X, std::uint32_t Z) const {
        return (Z * Width) + X;
    }

    float TerrainTiledMeshBuilder::CalculateWorldHeight(const HeightFieldData& Field, const TerrainBuildDesc& Desc, std::uint32_t X, std::uint32_t Z) const {
        const std::uint32_t Index{ CalculateIndex(Field.Width, X, Z) };
        const float Height01{ std::clamp(Field.HeightValues[Index], 0.0f, 1.0f) };
        return Height01 * Desc.MaxHeight;
    }

    asset::Vec3 TerrainTiledMeshBuilder::CalculatePosition(const HeightFieldData& Field, const TerrainBuildDesc& Desc, std::uint32_t X, std::uint32_t Z) const {
        float PositionX{ static_cast<float>(X) * Desc.CellSizeX };
        float PositionZ{ static_cast<float>(Z) * Desc.CellSizeZ };
        if (Desc.CenterOrigin == true) {
            const float OffsetX{ (static_cast<float>(Field.Width) - 1.0f) * Desc.CellSizeX * 0.5f };
            const float OffsetZ{ (static_cast<float>(Field.Height) - 1.0f) * Desc.CellSizeZ * 0.5f };
            PositionX -= OffsetX;
            PositionZ -= OffsetZ;
        }

        return asset::Vec3{ PositionX, CalculateWorldHeight(Field, Desc, X, Z), PositionZ };
    }

    asset::Vec2 TerrainTiledMeshBuilder::CalculateTexCoord(const HeightFieldData& Field, const TerrainBuildDesc& Desc, std::uint32_t X, std::uint32_t Z) const {
        (void)Field;

        const float U{ (static_cast<float>(Desc.mProceduralHeightFieldDesc.mSampleOffsetX) + static_cast<float>(X)) * Desc.CellSizeX };
        float V{ (static_cast<float>(Desc.mProceduralHeightFieldDesc.mSampleOffsetZ) + static_cast<float>(Z)) * Desc.CellSizeZ };
        if (Desc.FlipV == true) {
            V = -V;
        }

        return asset::Vec2{ U, V };
    }

    void TerrainTiledMeshBuilder::WritePatchVertex(asset::VertexAttributes& Vertices, std::uint32_t VertexIndex, const HeightFieldData& Field, const TerrainBuildDesc& Desc, std::uint32_t X, std::uint32_t Z) const {
        Vertices.Positions[VertexIndex] = CalculatePosition(Field, Desc, X, Z);
        Vertices.TexCoords[0][VertexIndex] = CalculateTexCoord(Field, Desc, X, Z);
        Vertices.Normals[VertexIndex] = asset::Vec3::Up;
        Vertices.Colors[VertexIndex] = asset::Vec4{ 0.0f, 1.0f, 0.0f, 1.0f };
    }

    DirectX::BoundingOrientedBox TerrainTiledMeshBuilder::BuildBoundingBoxFromRange(const HeightFieldData& Field, const TerrainBuildDesc& Desc, std::uint32_t StartX, std::uint32_t StartZ, std::uint32_t EndX, std::uint32_t EndZ) const {
        asset::Vec3 MinValue{ (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)() };
        asset::Vec3 MaxValue{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };

        for (std::uint32_t GridZ{ StartZ }; GridZ <= EndZ; ++GridZ) {
            for (std::uint32_t GridX{ StartX }; GridX <= EndX; ++GridX) {
                const asset::Vec3 Position{ CalculatePosition(Field, Desc, GridX, GridZ) };
                MinValue.x = std::min(MinValue.x, Position.x);
                MinValue.y = std::min(MinValue.y, Position.y);
                MinValue.z = std::min(MinValue.z, Position.z);
                MaxValue.x = std::max(MaxValue.x, Position.x);
                MaxValue.y = std::max(MaxValue.y, Position.y);
                MaxValue.z = std::max(MaxValue.z, Position.z);
            }
        }

        return BuildObbFromMinMax(MinValue, MaxValue);
    }
}
