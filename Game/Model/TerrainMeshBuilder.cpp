#include "TerrainMeshBuilder.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace Game {
    TerrainMeshBuilder::TerrainMeshBuilder() {
    }

    TerrainMeshBuilder::~TerrainMeshBuilder() {
    }

    TerrainMeshBuilder::TerrainMeshBuilder(const TerrainMeshBuilder& Other) {
        (void)Other;
    }

    TerrainMeshBuilder& TerrainMeshBuilder::operator=(const TerrainMeshBuilder& Other) {
        (void)Other;
        return *this;
    }

    TerrainMeshBuilder::TerrainMeshBuilder(TerrainMeshBuilder&& Other) noexcept {
        (void)Other;
    }

    TerrainMeshBuilder& TerrainMeshBuilder::operator=(TerrainMeshBuilder&& Other) noexcept {
        (void)Other;
        return *this;
    }

    TerrainMeshData TerrainMeshBuilder::Build(const HeightFieldData& Field, const TerrainBuildDesc& Desc) const {
        ValidateBuildInput(Field, Desc);

        TerrainMeshData Mesh{};
        const std::uint32_t VertexCountX{ Field.Width };
        const std::uint32_t VertexCountY{ Field.Height };
        const std::uint32_t QuadCountX{ Field.Width - 1 };
        const std::uint32_t QuadCountY{ Field.Height - 1 };

        const std::size_t VertexCount{ static_cast<std::size_t>(VertexCountX) * static_cast<std::size_t>(VertexCountY) };
        const std::size_t IndexCount{ static_cast<std::size_t>(QuadCountX) * static_cast<std::size_t>(QuadCountY) * 6ULL };

        Mesh.Vertices.Resize(VertexCount);
        Mesh.Indices.resize(IndexCount);

        const float OffsetX{ (static_cast<float>(Field.Width) - 1.0f) * Desc.CellSizeX * 0.5f };
        const float OffsetZ{ (static_cast<float>(Field.Height) - 1.0f) * Desc.CellSizeZ * 0.5f };

        for (std::uint32_t GridY{ 0 }; GridY < VertexCountY; ++GridY) {
            for (std::uint32_t GridX{ 0 }; GridX < VertexCountX; ++GridX) {
                const std::uint32_t VertexIndex{ CalculateIndex(Field.Width, GridX, GridY) };
                float PositionX{ static_cast<float>(GridX) * Desc.CellSizeX };
                float PositionZ{ static_cast<float>(GridY) * Desc.CellSizeZ };
                if (Desc.CenterOrigin == true) {
                    PositionX -= OffsetX;
                    PositionZ -= OffsetZ;
                }

                const float PositionY{ CalculateWorldHeight(Field, Desc, GridX, GridY) };
                Mesh.Vertices.Positions[VertexIndex] = asset::Vec3{ PositionX, PositionY, PositionZ };

                float U{ 0.0f };
                float V{ 0.0f };
                if (Field.Width > 1) {
                    U = static_cast<float>(GridX) / static_cast<float>(Field.Width - 1);
                }

                if (Field.Height > 1) {
                    V = static_cast<float>(GridY) / static_cast<float>(Field.Height - 1);
                }

                if (Desc.FlipV == true) {
                    V = 1.0f - V;
                }

                Mesh.Vertices.TexCoords[0][VertexIndex] = asset::Vec2{ U, V };
                Mesh.Vertices.Normals[VertexIndex] = asset::Vec3::Up;
                Mesh.Vertices.Colors[VertexIndex] = asset::Vec4{ 0.0f, 1.0f, 0.0f, 1.0f };
            }
        }

        std::size_t IndexWriteCursor{ 0 };
        for (std::uint32_t GridY{ 0 }; GridY < QuadCountY; ++GridY) {
            for (std::uint32_t GridX{ 0 }; GridX < QuadCountX; ++GridX) {
                const std::uint32_t I0{ CalculateIndex(Field.Width, GridX, GridY) };
                const std::uint32_t I1{ CalculateIndex(Field.Width, GridX + 1, GridY) };
                const std::uint32_t I2{ CalculateIndex(Field.Width, GridX, GridY + 1) };
                const std::uint32_t I3{ CalculateIndex(Field.Width, GridX + 1, GridY + 1) };

                Mesh.Indices[IndexWriteCursor] = I0;
                ++IndexWriteCursor;
                Mesh.Indices[IndexWriteCursor] = I2;
                ++IndexWriteCursor;
                Mesh.Indices[IndexWriteCursor] = I1;
                ++IndexWriteCursor;

                Mesh.Indices[IndexWriteCursor] = I1;
                ++IndexWriteCursor;
                Mesh.Indices[IndexWriteCursor] = I2;
                ++IndexWriteCursor;
                Mesh.Indices[IndexWriteCursor] = I3;
                ++IndexWriteCursor;
            }
        }

        for (std::size_t VertexIndex{ 0 }; VertexIndex < Mesh.Vertices.Normals.size(); ++VertexIndex) {
            Mesh.Vertices.Normals[VertexIndex] = asset::Vec3::Zero;
        }

        for (std::size_t IndexOffset{ 0 }; IndexOffset < Mesh.Indices.size(); IndexOffset += 3ULL) {
            const std::uint32_t Index0{ Mesh.Indices[IndexOffset] };
            const std::uint32_t Index1{ Mesh.Indices[IndexOffset + 1ULL] };
            const std::uint32_t Index2{ Mesh.Indices[IndexOffset + 2ULL] };

            const asset::Vec3& Position0{ Mesh.Vertices.Positions[Index0] };
            const asset::Vec3& Position1{ Mesh.Vertices.Positions[Index1] };
            const asset::Vec3& Position2{ Mesh.Vertices.Positions[Index2] };

            const asset::Vec3 Edge01{ Position1.x - Position0.x, Position1.y - Position0.y, Position1.z - Position0.z };
            const asset::Vec3 Edge02{ Position2.x - Position0.x, Position2.y - Position0.y, Position2.z - Position0.z };

            const asset::Vec3 FaceNormal{
                (Edge01.y * Edge02.z) - (Edge01.z * Edge02.y),
                (Edge01.z * Edge02.x) - (Edge01.x * Edge02.z),
                (Edge01.x * Edge02.y) - (Edge01.y * Edge02.x)
            };

            Mesh.Vertices.Normals[Index0] += FaceNormal;
            Mesh.Vertices.Normals[Index1] += FaceNormal;
            Mesh.Vertices.Normals[Index2] += FaceNormal;
        }

        for (std::size_t VertexIndex{ 0 }; VertexIndex < Mesh.Vertices.Normals.size(); ++VertexIndex) {
            const float LengthSquared{ Mesh.Vertices.Normals[VertexIndex].LengthSquared() };
            if (LengthSquared > 0.0f) {
                Mesh.Vertices.Normals[VertexIndex].Normalize();
            }

            else {
                Mesh.Vertices.Normals[VertexIndex] = asset::Vec3::Up;
            }
        }

        return Mesh;
    }

    void TerrainMeshBuilder::ValidateBuildInput(const HeightFieldData& Field, const TerrainBuildDesc& Desc) const {
        if (Desc.MaxHeight <= 0.0f) {
            throw std::runtime_error{ "MaxHeight must be greater than zero." };
        }

        if (Desc.CellSizeX <= 0.0f) {
            throw std::runtime_error{ "CellSizeX must be greater than zero." };
        }

        if (Desc.CellSizeZ <= 0.0f) {
            throw std::runtime_error{ "CellSizeZ must be greater than zero." };
        }

        if (Field.Width < 2 || Field.Height < 2) {
            throw std::runtime_error{ "Height field size must be at least 2x2." };
        }

        const std::size_t ExpectedSize{ static_cast<std::size_t>(Field.Width) * static_cast<std::size_t>(Field.Height) };
        if (Field.HeightValues.size() != ExpectedSize) {
            throw std::runtime_error{ "Height01 buffer size mismatch." };
        }
    }

    std::uint32_t TerrainMeshBuilder::CalculateIndex(std::uint32_t Width, std::uint32_t X, std::uint32_t Y) const {
        return (Y * Width) + X;
    }

    float TerrainMeshBuilder::CalculateWorldHeight(const HeightFieldData& Field, const TerrainBuildDesc& Desc, std::uint32_t X, std::uint32_t Y) const {
        const std::uint32_t Index{ CalculateIndex(Field.Width, X, Y) };
        const float Height01{ std::clamp(Field.HeightValues[Index], 0.0f, 1.0f) };
        return Height01 * Desc.MaxHeight;
    }

    SimpleMath::Vector3 TerrainMeshBuilder::CalculateNormal(const HeightFieldData& Field, const TerrainBuildDesc& Desc, std::uint32_t X, std::uint32_t Y) const {
        const std::uint32_t LeftX{ ClampCoordinate(static_cast<std::int32_t>(X) - 1, Field.Width - 1) };
        const std::uint32_t RightX{ ClampCoordinate(static_cast<std::int32_t>(X) + 1, Field.Width - 1) };
        const std::uint32_t DownY{ ClampCoordinate(static_cast<std::int32_t>(Y) - 1, Field.Height - 1) };
        const std::uint32_t UpY{ ClampCoordinate(static_cast<std::int32_t>(Y) + 1, Field.Height - 1) };

        const float HeightLeft{ CalculateWorldHeight(Field, Desc, LeftX, Y) };
        const float HeightRight{ CalculateWorldHeight(Field, Desc, RightX, Y) };
        const float HeightDown{ CalculateWorldHeight(Field, Desc, X, DownY) };
        const float HeightUp{ CalculateWorldHeight(Field, Desc, X, UpY) };

        const float Dx{ (HeightRight - HeightLeft) / (2.0f * Desc.CellSizeX) };
        const float Dz{ (HeightUp - HeightDown) / (2.0f * Desc.CellSizeZ) };

        SimpleMath::Vector3 Normal{ -Dx, 1.0f, -Dz };
        Normal.Normalize();
        return Normal;
    }

    std::uint32_t TerrainMeshBuilder::ClampCoordinate(std::int32_t Value, std::uint32_t MaxValue) const {
        const std::int32_t ClampedValue{ std::clamp(Value, 0, static_cast<std::int32_t>(MaxValue)) };
        return static_cast<std::uint32_t>(ClampedValue);
    }
}
