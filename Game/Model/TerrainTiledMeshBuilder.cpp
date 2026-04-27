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

namespace Game {
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

        const std::uint32_t VertexCountX{ Field.Width };
        const std::uint32_t VertexCountZ{ Field.Height };
        const std::uint32_t QuadCountX{ Field.Width - 1 };
        const std::uint32_t QuadCountZ{ Field.Height - 1 };
        const std::size_t VertexCount{ static_cast<std::size_t>(VertexCountX) * static_cast<std::size_t>(VertexCountZ) };

        TiledData.mTileCountX = (QuadCountX + Desc.TileQuadCount - 1u) / Desc.TileQuadCount;
        TiledData.mTileCountZ = (QuadCountZ + Desc.TileQuadCount - 1u) / Desc.TileQuadCount;
        TiledData.mTileMetadata.reserve(static_cast<std::size_t>(TiledData.mTileCountX) * static_cast<std::size_t>(TiledData.mTileCountZ));
        std::vector<std::uint32_t> NormalIndices{};

        asset::ModelNode& RootNode{ TiledData.mModelData.CreateNode("Terrain", nullptr) };
        RootNode.Vertices().Resize(VertexCount);

        for (std::uint32_t GridZ{ 0 }; GridZ < VertexCountZ; ++GridZ) {
            for (std::uint32_t GridX{ 0 }; GridX < VertexCountX; ++GridX) {
                const std::uint32_t VertexIndex{ CalculateIndex(Field.Width, GridX, GridZ) };
                RootNode.Vertices().Positions[VertexIndex] = CalculatePosition(Field, Desc, GridX, GridZ);
                RootNode.Vertices().TexCoords[0][VertexIndex] = CalculateTexCoord(Field, Desc, GridX, GridZ);
                RootNode.Vertices().Normals[VertexIndex] = asset::Vec3::Zero;
                RootNode.Vertices().Colors[VertexIndex] = asset::Vec4{ 0.0f, 1.0f, 0.0f, 1.0f };
            }
        }

        for (std::uint32_t TileZ{ 0 }; TileZ < TiledData.mTileCountZ; ++TileZ) {
            for (std::uint32_t TileX{ 0 }; TileX < TiledData.mTileCountX; ++TileX) {
                const std::uint32_t StartX{ TileX * Desc.TileQuadCount };
                const std::uint32_t StartZ{ TileZ * Desc.TileQuadCount };
                const std::uint32_t EndX{ (std::min)(StartX + Desc.TileQuadCount, QuadCountX) };
                const std::uint32_t EndZ{ (std::min)(StartZ + Desc.TileQuadCount, QuadCountZ) };

                TerrainTileMetadata TileMetadata{};
                TileMetadata.mTileIndexX = TileX;
                TileMetadata.mTileIndexZ = TileZ;
                TileMetadata.mLocalBoundingBox = BuildBoundingBoxFromRange(RootNode.Vertices(), Field.Width, StartX, StartZ, EndX, EndZ);
                TileMetadata.mCenter = SimpleMath::Vector3{ TileMetadata.mLocalBoundingBox.Center.x, TileMetadata.mLocalBoundingBox.Center.y, TileMetadata.mLocalBoundingBox.Center.z };

                for (std::uint32_t LodIndex{ 0 }; LodIndex < TiledData.mLodCount; ++LodIndex) {
                    const std::size_t IndexOffset{ RootNode.Indices().size() };
                    const std::uint32_t LodStride{ CalculateLodStride(LodIndex) };
                    AppendTileLodIndices(RootNode.Indices(), Field.Width, StartX, StartZ, EndX, EndZ, LodStride);

                    asset::ModelNode::SubMesh SubMesh{};
                    SubMesh.IndexOffset = IndexOffset;
                    SubMesh.IndexCount = RootNode.Indices().size() - IndexOffset;
                    SubMesh.MaterialGroupItemIndex = 0;
                    RootNode.SubMeshes().push_back(SubMesh);

                    const std::uint32_t SubMeshIndex{ static_cast<std::uint32_t>(RootNode.SubMeshes().size() - 1ULL) };
                    if (LodIndex == 0u) {
                        TileMetadata.mSubMeshIndex = SubMeshIndex;
                        NormalIndices.insert(NormalIndices.end(), RootNode.Indices().begin() + static_cast<std::ptrdiff_t>(IndexOffset), RootNode.Indices().end());
                    }

                    TileMetadata.mSubMeshIndexByLod.push_back(SubMeshIndex);
                }

                TiledData.mTileMetadata.push_back(std::move(TileMetadata));
            }
        }

        AccumulateNormals(RootNode.Vertices(), NormalIndices);
        TiledData.mLocalBoundingBox = BuildBoundingBoxFromAllVertices(RootNode.Vertices());
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

    std::uint32_t TerrainTiledMeshBuilder::CalculateLodStride(std::uint32_t LodIndex) const {
        std::uint32_t Stride{ 1 };
        for (std::uint32_t Index{ 0 }; Index < LodIndex; ++Index) {
            if (Stride >= 1024u) {
                return Stride;
            }

            Stride *= 2u;
        }

        return Stride;
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
        float U{ 0.0f };
        float V{ 0.0f };
        if (Field.Width > 1u) {
            U = static_cast<float>(X) / static_cast<float>(Field.Width - 1u);
        }

        if (Field.Height > 1u) {
            V = static_cast<float>(Z) / static_cast<float>(Field.Height - 1u);
        }

        if (Desc.FlipV == true) {
            V = 1.0f - V;
        }

        return asset::Vec2{ U, V };
    }

    void TerrainTiledMeshBuilder::AppendTileLodIndices(std::vector<std::uint32_t>& OutIndices, std::uint32_t FieldWidth, std::uint32_t StartX, std::uint32_t StartZ, std::uint32_t EndX, std::uint32_t EndZ, std::uint32_t LodStride) const {
        const std::uint32_t EffectiveStride{ (std::max)(LodStride, 1u) };

        for (std::uint32_t GridZ{ StartZ }; GridZ < EndZ;) {
            const std::uint32_t NextZ{ (std::min)(GridZ + EffectiveStride, EndZ) };

            for (std::uint32_t GridX{ StartX }; GridX < EndX;) {
                const std::uint32_t NextX{ (std::min)(GridX + EffectiveStride, EndX) };
                const std::uint32_t I0{ CalculateIndex(FieldWidth, GridX, GridZ) };
                const std::uint32_t I1{ CalculateIndex(FieldWidth, NextX, GridZ) };
                const std::uint32_t I2{ CalculateIndex(FieldWidth, GridX, NextZ) };
                const std::uint32_t I3{ CalculateIndex(FieldWidth, NextX, NextZ) };

                OutIndices.push_back(I0);
                OutIndices.push_back(I2);
                OutIndices.push_back(I1);
                OutIndices.push_back(I1);
                OutIndices.push_back(I2);
                OutIndices.push_back(I3);

                GridX = NextX;
            }

            GridZ = NextZ;
        }
    }

    DirectX::BoundingOrientedBox TerrainTiledMeshBuilder::BuildBoundingBoxFromRange(const asset::VertexAttributes& Vertices, std::uint32_t FieldWidth, std::uint32_t StartX, std::uint32_t StartZ, std::uint32_t EndX, std::uint32_t EndZ) const {
        asset::Vec3 MinValue{ (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)() };
        asset::Vec3 MaxValue{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };

        for (std::uint32_t GridZ{ StartZ }; GridZ <= EndZ; ++GridZ) {
            for (std::uint32_t GridX{ StartX }; GridX <= EndX; ++GridX) {
                const std::uint32_t VertexIndex{ CalculateIndex(FieldWidth, GridX, GridZ) };
                const asset::Vec3& Position{ Vertices.Positions[VertexIndex] };
                MinValue.x = (std::min)(MinValue.x, Position.x);
                MinValue.y = (std::min)(MinValue.y, Position.y);
                MinValue.z = (std::min)(MinValue.z, Position.z);
                MaxValue.x = (std::max)(MaxValue.x, Position.x);
                MaxValue.y = (std::max)(MaxValue.y, Position.y);
                MaxValue.z = (std::max)(MaxValue.z, Position.z);
            }
        }

        return BuildObbFromMinMax(MinValue, MaxValue);
    }

    DirectX::BoundingOrientedBox TerrainTiledMeshBuilder::BuildBoundingBoxFromAllVertices(const asset::VertexAttributes& Vertices) const {
        asset::Vec3 MinValue{ (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)() };
        asset::Vec3 MaxValue{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };

        for (const asset::Vec3& Position : Vertices.Positions) {
            MinValue.x = (std::min)(MinValue.x, Position.x);
            MinValue.y = (std::min)(MinValue.y, Position.y);
            MinValue.z = (std::min)(MinValue.z, Position.z);
            MaxValue.x = (std::max)(MaxValue.x, Position.x);
            MaxValue.y = (std::max)(MaxValue.y, Position.y);
            MaxValue.z = (std::max)(MaxValue.z, Position.z);
        }

        return BuildObbFromMinMax(MinValue, MaxValue);
    }

    void TerrainTiledMeshBuilder::AccumulateNormals(asset::VertexAttributes& Vertices, const std::vector<std::uint32_t>& Indices) const {
        for (std::size_t VertexIndex{ 0 }; VertexIndex < Vertices.Normals.size(); ++VertexIndex) {
            Vertices.Normals[VertexIndex] = asset::Vec3::Zero;
        }

        for (std::size_t IndexOffset{ 0 }; IndexOffset < Indices.size(); IndexOffset += 3ULL) {
            const std::uint32_t Index0{ Indices[IndexOffset] };
            const std::uint32_t Index1{ Indices[IndexOffset + 1ULL] };
            const std::uint32_t Index2{ Indices[IndexOffset + 2ULL] };

            const asset::Vec3& Position0{ Vertices.Positions[Index0] };
            const asset::Vec3& Position1{ Vertices.Positions[Index1] };
            const asset::Vec3& Position2{ Vertices.Positions[Index2] };

            const asset::Vec3 Edge01{ Position1.x - Position0.x, Position1.y - Position0.y, Position1.z - Position0.z };
            const asset::Vec3 Edge02{ Position2.x - Position0.x, Position2.y - Position0.y, Position2.z - Position0.z };
            const asset::Vec3 FaceNormal{ (Edge01.y * Edge02.z) - (Edge01.z * Edge02.y), (Edge01.z * Edge02.x) - (Edge01.x * Edge02.z), (Edge01.x * Edge02.y) - (Edge01.y * Edge02.x) };

            Vertices.Normals[Index0] += FaceNormal;
            Vertices.Normals[Index1] += FaceNormal;
            Vertices.Normals[Index2] += FaceNormal;
        }

        for (std::size_t VertexIndex{ 0 }; VertexIndex < Vertices.Normals.size(); ++VertexIndex) {
            const float LengthSquared{ Vertices.Normals[VertexIndex].LengthSquared() };
            if (LengthSquared > 0.0f) {
                Vertices.Normals[VertexIndex].Normalize();
            }
            else {
                Vertices.Normals[VertexIndex] = asset::Vec3::Up;
            }
        }
    }
}
