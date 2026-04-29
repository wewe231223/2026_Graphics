#include "PrimitiveModelFactory.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <limits>
#include <sstream>
#include <string>
#include <vector>
#include "TerrainHeightFieldFactory.h"
#include "TerrainMeshBuilder.h"
#include "TerrainMeshTypes.h"

namespace {
    constexpr float Pi{ 3.14159265358979323846f };

    struct MeshData final {
        asset::VertexAttributes Vertices{};
        std::vector<std::uint32_t> Indices{};
    };

    struct PrimitiveParameters final {
        float Size{ 1.0f };
        asset::Vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    struct TerrainSelectorData final {
        Game::TerrainBuildDesc Desc{};
    };

    asset::Vec3 ScalePosition(const asset::Vec3& Position, float Size) {
        return asset::Vec3{ Position.x * Size, Position.y * Size, Position.z * Size };
    }

    bool TryParseFloat(const std::string& Text, float& OutValue) {
        const char* Begin{ Text.c_str() };
        char* End{ nullptr };
        const float Value{ std::strtof(Begin, &End) };
        if (End == Begin || *End != '\0') {
            return false;
        }

        OutValue = Value;
        return true;
    }

    bool TryParseBool(const std::string& Text, bool& OutValue) {
        if (Text == "true" || Text == "1") {
            OutValue = true;
            return true;
        }

        if (Text == "false" || Text == "0") {
            OutValue = false;
            return true;
        }

        return false;
    }

    bool TryParseUInt32(const std::string& Text, std::uint32_t& OutValue) {
        const char* Begin{ Text.c_str() };
        char* End{ nullptr };
        const unsigned long Value{ std::strtoul(Begin, &End, 10) };
        if (End == Begin || *End != '\0' || Value > static_cast<unsigned long>((std::numeric_limits<std::uint32_t>::max)())) {
            return false;
        }

        OutValue = static_cast<std::uint32_t>(Value);
        return true;
    }

    void AddVertex(MeshData& Data, const asset::Vec3& Position, const asset::Vec3& Normal, const asset::Vec2& TexCoord, const asset::Vec4& Color) {
        Data.Vertices.Positions.push_back(Position);
        Data.Vertices.Normals.push_back(Normal);
        Data.Vertices.TexCoords[0].push_back(TexCoord);
        Data.Vertices.Colors.push_back(Color);
    }

    void AddTriangle(MeshData& Data, std::uint32_t A, std::uint32_t B, std::uint32_t C) {
        Data.Indices.push_back(A);
        Data.Indices.push_back(B);
        Data.Indices.push_back(C);
    }

    MeshData CreateBox(const PrimitiveParameters& Parameters) {
        MeshData Data{};
        const float Half{ 0.5f };

        const std::array<asset::Vec3, 24> Positions{ {
            asset::Vec3{ -Half, -Half, Half }, asset::Vec3{ Half, -Half, Half }, asset::Vec3{ Half, Half, Half }, asset::Vec3{ -Half, Half, Half },
            asset::Vec3{ Half, -Half, -Half }, asset::Vec3{ -Half, -Half, -Half }, asset::Vec3{ -Half, Half, -Half }, asset::Vec3{ Half, Half, -Half },
            asset::Vec3{ -Half, -Half, -Half }, asset::Vec3{ -Half, -Half, Half }, asset::Vec3{ -Half, Half, Half }, asset::Vec3{ -Half, Half, -Half },
            asset::Vec3{ Half, -Half, Half }, asset::Vec3{ Half, -Half, -Half }, asset::Vec3{ Half, Half, -Half }, asset::Vec3{ Half, Half, Half },
            asset::Vec3{ -Half, Half, Half }, asset::Vec3{ Half, Half, Half }, asset::Vec3{ Half, Half, -Half }, asset::Vec3{ -Half, Half, -Half },
            asset::Vec3{ -Half, -Half, -Half }, asset::Vec3{ Half, -Half, -Half }, asset::Vec3{ Half, -Half, Half }, asset::Vec3{ -Half, -Half, Half }
        } };

        const std::array<asset::Vec3, 24> Normals{ {
            asset::Vec3{ 0.0f, 0.0f, 1.0f }, asset::Vec3{ 0.0f, 0.0f, 1.0f }, asset::Vec3{ 0.0f, 0.0f, 1.0f }, asset::Vec3{ 0.0f, 0.0f, 1.0f },
            asset::Vec3{ 0.0f, 0.0f, -1.0f }, asset::Vec3{ 0.0f, 0.0f, -1.0f }, asset::Vec3{ 0.0f, 0.0f, -1.0f }, asset::Vec3{ 0.0f, 0.0f, -1.0f },
            asset::Vec3{ -1.0f, 0.0f, 0.0f }, asset::Vec3{ -1.0f, 0.0f, 0.0f }, asset::Vec3{ -1.0f, 0.0f, 0.0f }, asset::Vec3{ -1.0f, 0.0f, 0.0f },
            asset::Vec3{ 1.0f, 0.0f, 0.0f }, asset::Vec3{ 1.0f, 0.0f, 0.0f }, asset::Vec3{ 1.0f, 0.0f, 0.0f }, asset::Vec3{ 1.0f, 0.0f, 0.0f },
            asset::Vec3{ 0.0f, 1.0f, 0.0f }, asset::Vec3{ 0.0f, 1.0f, 0.0f }, asset::Vec3{ 0.0f, 1.0f, 0.0f }, asset::Vec3{ 0.0f, 1.0f, 0.0f },
            asset::Vec3{ 0.0f, -1.0f, 0.0f }, asset::Vec3{ 0.0f, -1.0f, 0.0f }, asset::Vec3{ 0.0f, -1.0f, 0.0f }, asset::Vec3{ 0.0f, -1.0f, 0.0f }
        } };

        const std::array<asset::Vec2, 4> FaceUv{ { asset::Vec2{ 0.0f, 1.0f }, asset::Vec2{ 1.0f, 1.0f }, asset::Vec2{ 1.0f, 0.0f }, asset::Vec2{ 0.0f, 0.0f } } };

        Data.Vertices.Reserve(24);
        for (std::size_t Index{ 0 }; Index < Positions.size(); ++Index) {
            AddVertex(Data, ScalePosition(Positions[Index], Parameters.Size), Normals[Index], FaceUv[Index % 4], Parameters.Color);
        }

        for (std::uint32_t FaceIndex{ 0 }; FaceIndex < 6; ++FaceIndex) {
            const std::uint32_t Base{ FaceIndex * 4 };
            AddTriangle(Data, Base + 0, Base + 1, Base + 2);
            AddTriangle(Data, Base + 0, Base + 2, Base + 3);
        }

        return Data;
    }

    MeshData CreateSphere(const PrimitiveParameters& Parameters) {
        MeshData Data{};
        const std::uint32_t SliceCount{ 32 };
        const std::uint32_t StackCount{ 16 };
        const float Radius{ 0.5f * Parameters.Size };

        const std::size_t VertexCount{ static_cast<std::size_t>(StackCount + 1) * static_cast<std::size_t>(SliceCount + 1) };
        Data.Vertices.Reserve(VertexCount);

        for (std::uint32_t StackIndex{ 0 }; StackIndex <= StackCount; ++StackIndex) {
            const float V{ static_cast<float>(StackIndex) / static_cast<float>(StackCount) };
            const float Phi{ Pi * V };
            const float SinPhi{ std::sin(Phi) };
            const float CosPhi{ std::cos(Phi) };

            for (std::uint32_t SliceIndex{ 0 }; SliceIndex <= SliceCount; ++SliceIndex) {
                const float U{ static_cast<float>(SliceIndex) / static_cast<float>(SliceCount) };
                const float Theta{ 2.0f * Pi * U };
                const float SinTheta{ std::sin(Theta) };
                const float CosTheta{ std::cos(Theta) };

                const asset::Vec3 Normal{ SinPhi * CosTheta, CosPhi, SinPhi * SinTheta };
                const asset::Vec3 Position{ Radius * Normal.x, Radius * Normal.y, Radius * Normal.z };
                const asset::Vec2 TexCoord{ U, V };
                AddVertex(Data, Position, Normal, TexCoord, Parameters.Color);
            }
        }

        for (std::uint32_t StackIndex{ 0 }; StackIndex < StackCount; ++StackIndex) {
            for (std::uint32_t SliceIndex{ 0 }; SliceIndex < SliceCount; ++SliceIndex) {
                const std::uint32_t I0{ StackIndex * (SliceCount + 1) + SliceIndex };
                const std::uint32_t I1{ I0 + 1 };
                const std::uint32_t I2{ I0 + (SliceCount + 1) };
                const std::uint32_t I3{ I2 + 1 };
                AddTriangle(Data, I0, I1, I2);
                AddTriangle(Data, I1, I3, I2);
            }
        }

        return Data;
    }

    MeshData CreateCylinder(const PrimitiveParameters& Parameters) {
        MeshData Data{};
        const std::uint32_t SliceCount{ 32 };
        const float Radius{ 0.5f * Parameters.Size };
        const float HalfHeight{ 0.5f * Parameters.Size };

        for (std::uint32_t SliceIndex{ 0 }; SliceIndex <= SliceCount; ++SliceIndex) {
            const float U{ static_cast<float>(SliceIndex) / static_cast<float>(SliceCount) };
            const float Theta{ 2.0f * Pi * U };
            const float SinTheta{ std::sin(Theta) };
            const float CosTheta{ std::cos(Theta) };
            const asset::Vec3 Normal{ CosTheta, 0.0f, SinTheta };
            AddVertex(Data, asset::Vec3{ Radius * CosTheta, -HalfHeight, Radius * SinTheta }, Normal, asset::Vec2{ U, 1.0f }, Parameters.Color);
            AddVertex(Data, asset::Vec3{ Radius * CosTheta, HalfHeight, Radius * SinTheta }, Normal, asset::Vec2{ U, 0.0f }, Parameters.Color);
        }

        for (std::uint32_t SliceIndex{ 0 }; SliceIndex < SliceCount; ++SliceIndex) {
            const std::uint32_t Base{ SliceIndex * 2 };
            AddTriangle(Data, Base + 0, Base + 1, Base + 3);
            AddTriangle(Data, Base + 0, Base + 3, Base + 2);
        }

        const std::uint32_t TopCenterIndex{ static_cast<std::uint32_t>(Data.Vertices.VertexCount()) };
        AddVertex(Data, asset::Vec3{ 0.0f, HalfHeight, 0.0f }, asset::Vec3{ 0.0f, 1.0f, 0.0f }, asset::Vec2{ 0.5f, 0.5f }, Parameters.Color);
        for (std::uint32_t SliceIndex{ 0 }; SliceIndex <= SliceCount; ++SliceIndex) {
            const float U{ static_cast<float>(SliceIndex) / static_cast<float>(SliceCount) };
            const float Theta{ 2.0f * Pi * U };
            const float SinTheta{ std::sin(Theta) };
            const float CosTheta{ std::cos(Theta) };
            AddVertex(Data, asset::Vec3{ Radius * CosTheta, HalfHeight, Radius * SinTheta }, asset::Vec3{ 0.0f, 1.0f, 0.0f }, asset::Vec2{ 0.5f + 0.5f * CosTheta, 0.5f - 0.5f * SinTheta }, Parameters.Color);
        }

        for (std::uint32_t SliceIndex{ 0 }; SliceIndex < SliceCount; ++SliceIndex) {
            AddTriangle(Data, TopCenterIndex, TopCenterIndex + SliceIndex + 2, TopCenterIndex + SliceIndex + 1);
        }

        const std::uint32_t BottomCenterIndex{ static_cast<std::uint32_t>(Data.Vertices.VertexCount()) };
        AddVertex(Data, asset::Vec3{ 0.0f, -HalfHeight, 0.0f }, asset::Vec3{ 0.0f, -1.0f, 0.0f }, asset::Vec2{ 0.5f, 0.5f }, Parameters.Color);
        for (std::uint32_t SliceIndex{ 0 }; SliceIndex <= SliceCount; ++SliceIndex) {
            const float U{ static_cast<float>(SliceIndex) / static_cast<float>(SliceCount) };
            const float Theta{ 2.0f * Pi * U };
            const float SinTheta{ std::sin(Theta) };
            const float CosTheta{ std::cos(Theta) };
            AddVertex(Data, asset::Vec3{ Radius * CosTheta, -HalfHeight, Radius * SinTheta }, asset::Vec3{ 0.0f, -1.0f, 0.0f }, asset::Vec2{ 0.5f + 0.5f * CosTheta, 0.5f + 0.5f * SinTheta }, Parameters.Color);
        }

        for (std::uint32_t SliceIndex{ 0 }; SliceIndex < SliceCount; ++SliceIndex) {
            AddTriangle(Data, BottomCenterIndex, BottomCenterIndex + SliceIndex + 1, BottomCenterIndex + SliceIndex + 2);
        }

        return Data;
    }

    MeshData CreateCone(const PrimitiveParameters& Parameters) {
        MeshData Data{};
        const std::uint32_t SliceCount{ 32 };
        const float Radius{ 0.5f * Parameters.Size };
        const float HalfHeight{ 0.5f * Parameters.Size };

        const asset::Vec3 Apex{ 0.0f, HalfHeight, 0.0f };
        for (std::uint32_t SliceIndex{ 0 }; SliceIndex <= SliceCount; ++SliceIndex) {
            const float U{ static_cast<float>(SliceIndex) / static_cast<float>(SliceCount) };
            const float Theta{ 2.0f * Pi * U };
            const float SinTheta{ std::sin(Theta) };
            const float CosTheta{ std::cos(Theta) };
            const asset::Vec3 BasePoint{ Radius * CosTheta, -HalfHeight, Radius * SinTheta };
            const asset::Vec3 Tangent{ -SinTheta, 0.0f, CosTheta };
            const asset::Vec3 ToApex{ Apex.x - BasePoint.x, Apex.y - BasePoint.y, Apex.z - BasePoint.z };
            const asset::Vec3 Normal{ ToApex.y * Tangent.z - ToApex.z * Tangent.y, ToApex.z * Tangent.x - ToApex.x * Tangent.z, ToApex.x * Tangent.y - ToApex.y * Tangent.x };
            const float Length{ std::sqrt(Normal.x * Normal.x + Normal.y * Normal.y + Normal.z * Normal.z) };
            const asset::Vec3 UnitNormal{ Normal.x / Length, Normal.y / Length, Normal.z / Length };
            AddVertex(Data, BasePoint, UnitNormal, asset::Vec2{ U, 1.0f }, Parameters.Color);
            AddVertex(Data, Apex, UnitNormal, asset::Vec2{ U, 0.0f }, Parameters.Color);
        }

        for (std::uint32_t SliceIndex{ 0 }; SliceIndex < SliceCount; ++SliceIndex) {
            const std::uint32_t Base{ SliceIndex * 2 };
            AddTriangle(Data, Base + 0, Base + 1, Base + 2);
        }

        const std::uint32_t BottomCenterIndex{ static_cast<std::uint32_t>(Data.Vertices.VertexCount()) };
        AddVertex(Data, asset::Vec3{ 0.0f, -HalfHeight, 0.0f }, asset::Vec3{ 0.0f, -1.0f, 0.0f }, asset::Vec2{ 0.5f, 0.5f }, Parameters.Color);
        for (std::uint32_t SliceIndex{ 0 }; SliceIndex <= SliceCount; ++SliceIndex) {
            const float U{ static_cast<float>(SliceIndex) / static_cast<float>(SliceCount) };
            const float Theta{ 2.0f * Pi * U };
            const float SinTheta{ std::sin(Theta) };
            const float CosTheta{ std::cos(Theta) };
            AddVertex(Data, asset::Vec3{ Radius * CosTheta, -HalfHeight, Radius * SinTheta }, asset::Vec3{ 0.0f, -1.0f, 0.0f }, asset::Vec2{ 0.5f + 0.5f * CosTheta, 0.5f + 0.5f * SinTheta }, Parameters.Color);
        }

        for (std::uint32_t SliceIndex{ 0 }; SliceIndex < SliceCount; ++SliceIndex) {
            AddTriangle(Data, BottomCenterIndex, BottomCenterIndex + SliceIndex + 1, BottomCenterIndex + SliceIndex + 2);
        }

        return Data;
    }

    MeshData CreateTriangularPyramid(const PrimitiveParameters& Parameters) {
        MeshData Data{};

        const asset::Vec3 Base0{ -0.5f * Parameters.Size, -0.5f * Parameters.Size, -0.28867513f * Parameters.Size };
        const asset::Vec3 Base1{ 0.5f * Parameters.Size, -0.5f * Parameters.Size, -0.28867513f * Parameters.Size };
        const asset::Vec3 Base2{ 0.0f, -0.5f * Parameters.Size, 0.57735026f * Parameters.Size };
        const asset::Vec3 Apex{ 0.0f, 0.5f * Parameters.Size, 0.0f };

        const std::array<std::array<asset::Vec3, 3>, 4> Faces{ {
            std::array<asset::Vec3, 3>{ Base0, Base1, Base2 },
            std::array<asset::Vec3, 3>{ Base0, Apex, Base1 },
            std::array<asset::Vec3, 3>{ Base1, Apex, Base2 },
            std::array<asset::Vec3, 3>{ Base2, Apex, Base0 }
        } };

        const std::array<asset::Vec2, 3> FaceUv{ { asset::Vec2{ 0.0f, 1.0f }, asset::Vec2{ 0.5f, 0.0f }, asset::Vec2{ 1.0f, 1.0f } } };

        for (const std::array<asset::Vec3, 3>& Face : Faces) {
            const asset::Vec3 Edge0{ Face[1].x - Face[0].x, Face[1].y - Face[0].y, Face[1].z - Face[0].z };
            const asset::Vec3 Edge1{ Face[2].x - Face[0].x, Face[2].y - Face[0].y, Face[2].z - Face[0].z };
            const asset::Vec3 NormalRaw{ Edge0.y * Edge1.z - Edge0.z * Edge1.y, Edge0.z * Edge1.x - Edge0.x * Edge1.z, Edge0.x * Edge1.y - Edge0.y * Edge1.x };
            const float Length{ std::sqrt(NormalRaw.x * NormalRaw.x + NormalRaw.y * NormalRaw.y + NormalRaw.z * NormalRaw.z) };
            const asset::Vec3 Normal{ NormalRaw.x / Length, NormalRaw.y / Length, NormalRaw.z / Length };
            const std::uint32_t BaseIndex{ static_cast<std::uint32_t>(Data.Vertices.VertexCount()) };
            AddVertex(Data, Face[0], Normal, FaceUv[0], Parameters.Color);
            AddVertex(Data, Face[1], Normal, FaceUv[1], Parameters.Color);
            AddVertex(Data, Face[2], Normal, FaceUv[2], Parameters.Color);
            AddTriangle(Data, BaseIndex + 0, BaseIndex + 1, BaseIndex + 2);
        }

        return Data;
    }

    MeshData CreateSquarePyramid(const PrimitiveParameters& Parameters) {
        MeshData Data{};

        const asset::Vec3 Base0{ -0.5f * Parameters.Size, -0.5f * Parameters.Size, -0.5f * Parameters.Size };
        const asset::Vec3 Base1{ 0.5f * Parameters.Size, -0.5f * Parameters.Size, -0.5f * Parameters.Size };
        const asset::Vec3 Base2{ 0.5f * Parameters.Size, -0.5f * Parameters.Size, 0.5f * Parameters.Size };
        const asset::Vec3 Base3{ -0.5f * Parameters.Size, -0.5f * Parameters.Size, 0.5f * Parameters.Size };
        const asset::Vec3 Apex{ 0.0f, 0.5f * Parameters.Size, 0.0f };

        const std::array<std::array<asset::Vec3, 3>, 6> Faces{ {
            std::array<asset::Vec3, 3>{ Base0, Base1, Base2 },
            std::array<asset::Vec3, 3>{ Base0, Base2, Base3 },
            std::array<asset::Vec3, 3>{ Base0, Apex, Base1 },
            std::array<asset::Vec3, 3>{ Base1, Apex, Base2 },
            std::array<asset::Vec3, 3>{ Base2, Apex, Base3 },
            std::array<asset::Vec3, 3>{ Base3, Apex, Base0 }
        } };

        const std::array<asset::Vec2, 3> FaceUv{ { asset::Vec2{ 0.0f, 1.0f }, asset::Vec2{ 0.5f, 0.0f }, asset::Vec2{ 1.0f, 1.0f } } };

        for (const std::array<asset::Vec3, 3>& Face : Faces) {
            const asset::Vec3 Edge0{ Face[1].x - Face[0].x, Face[1].y - Face[0].y, Face[1].z - Face[0].z };
            const asset::Vec3 Edge1{ Face[2].x - Face[0].x, Face[2].y - Face[0].y, Face[2].z - Face[0].z };
            const asset::Vec3 NormalRaw{ Edge0.y * Edge1.z - Edge0.z * Edge1.y, Edge0.z * Edge1.x - Edge0.x * Edge1.z, Edge0.x * Edge1.y - Edge0.y * Edge1.x };
            const float Length{ std::sqrt(NormalRaw.x * NormalRaw.x + NormalRaw.y * NormalRaw.y + NormalRaw.z * NormalRaw.z) };
            const asset::Vec3 Normal{ NormalRaw.x / Length, NormalRaw.y / Length, NormalRaw.z / Length };
            const std::uint32_t BaseIndex{ static_cast<std::uint32_t>(Data.Vertices.VertexCount()) };
            AddVertex(Data, Face[0], Normal, FaceUv[0], Parameters.Color);
            AddVertex(Data, Face[1], Normal, FaceUv[1], Parameters.Color);
            AddVertex(Data, Face[2], Normal, FaceUv[2], Parameters.Color);
            AddTriangle(Data, BaseIndex + 0, BaseIndex + 1, BaseIndex + 2);
        }

        return Data;
    }

    MeshData CreateSkyDome(const PrimitiveParameters& Parameters) {
        MeshData Data{};
        const std::uint32_t SliceCount{ 48 };
        const std::uint32_t StackCount{ 24 };
        const float Radius{ 0.5f * Parameters.Size };
        const asset::Vec4 SkyDomeColor{ 0.5f, 0.5f, 0.5f, 1.0f };

        const std::size_t VertexCount{ static_cast<std::size_t>(StackCount + 1) * static_cast<std::size_t>(SliceCount + 1) };
        Data.Vertices.Reserve(VertexCount);

        for (std::uint32_t StackIndex{ 0 }; StackIndex <= StackCount; ++StackIndex) {
            const float V{ static_cast<float>(StackIndex) / static_cast<float>(StackCount) };
            const float Phi{ Pi * V };
            const float SinPhi{ std::sin(Phi) };
            const float CosPhi{ std::cos(Phi) };

            for (std::uint32_t SliceIndex{ 0 }; SliceIndex <= SliceCount; ++SliceIndex) {
                const float U{ static_cast<float>(SliceIndex) / static_cast<float>(SliceCount) };
                const float Theta{ 2.0f * Pi * U };
                const float SinTheta{ std::sin(Theta) };
                const float CosTheta{ std::cos(Theta) };
                const asset::Vec3 OutwardNormal{ SinPhi * CosTheta, CosPhi, SinPhi * SinTheta };
                const asset::Vec3 InwardNormal{ -OutwardNormal.x, -OutwardNormal.y, -OutwardNormal.z };
                const asset::Vec3 Position{ Radius * OutwardNormal.x, Radius * OutwardNormal.y, Radius * OutwardNormal.z };
                const asset::Vec2 TexCoord{ U, V };
                AddVertex(Data, Position, InwardNormal, TexCoord, SkyDomeColor);
            }
        }

        for (std::uint32_t StackIndex{ 0 }; StackIndex < StackCount; ++StackIndex) {
            for (std::uint32_t SliceIndex{ 0 }; SliceIndex < SliceCount; ++SliceIndex) {
                const std::uint32_t I0{ StackIndex * (SliceCount + 1) + SliceIndex };
                const std::uint32_t I1{ I0 + 1 };
                const std::uint32_t I2{ I0 + (SliceCount + 1) };
                const std::uint32_t I3{ I2 + 1 };
                AddTriangle(Data, I0, I2, I1);
                AddTriangle(Data, I1, I2, I3);
            }
        }

        return Data;
    }

    bool TryParseFloatList(const std::string& Values, std::vector<float>& OutValues) {
        std::stringstream Stream{ Values };
        std::string ValueToken{};

        while (std::getline(Stream, ValueToken, ',')) {
            if (ValueToken.empty()) {
                return false;
            }

            float ParsedValue{ 0.0f };
            const bool IsParsed{ TryParseFloat(ValueToken, ParsedValue) };
            if (IsParsed == false) {
                return false;
            }

            OutValues.push_back(ParsedValue);
        }

        return OutValues.empty() == false;
    }

    bool TryParseTerrainHeightSourceTypeText(const std::string& Text, Game::TerrainHeightSourceType& OutValue) {
        if (Text == "HeightMap" || Text == "heightmap" || Text == "HeightMapPath") {
            OutValue = Game::TerrainHeightSourceType::HeightMap;
            return true;
        }

        if (Text == "Procedural" || Text == "procedural") {
            OutValue = Game::TerrainHeightSourceType::Procedural;
            return true;
        }

        return false;
    }

    bool TryParseSelector(const std::string& Selector, std::string& OutPrimitiveType, PrimitiveParameters& OutParameters) {
        const std::size_t FirstSeparator{ Selector.find(';') };
        if (FirstSeparator == std::string::npos) {
            OutPrimitiveType = Selector;
            return true;
        }

        OutPrimitiveType = Selector.substr(0, FirstSeparator);
        std::size_t CurrentStart{ FirstSeparator + 1 };

        while (CurrentStart < Selector.size()) {
            const std::size_t TokenEnd{ Selector.find(';', CurrentStart) };
            const std::size_t TokenLength{ TokenEnd == std::string::npos ? Selector.size() - CurrentStart : TokenEnd - CurrentStart };
            const std::string Token{ Selector.substr(CurrentStart, TokenLength) };
            const std::size_t EqualsIndex{ Token.find('=') };
            if (EqualsIndex == std::string::npos || EqualsIndex == 0 || EqualsIndex + 1 >= Token.size()) {
                return false;
            }

            const std::string Key{ Token.substr(0, EqualsIndex) };
            const std::string Value{ Token.substr(EqualsIndex + 1) };

            if (Key == "size") {
                const bool IsParsedSize{ TryParseFloat(Value, OutParameters.Size) };
                if (IsParsedSize == false) {
                    return false;
                }
            }
            else if (Key == "color") {
                std::vector<float> Parsed{};
                const bool IsParsed{ TryParseFloatList(Value, Parsed) };
                if (IsParsed == false || Parsed.size() != 4) {
                    return false;
                }

                OutParameters.Color = asset::Vec4{ Parsed[0], Parsed[1], Parsed[2], Parsed[3] };
            }
            else {
                return false;
            }

            if (TokenEnd == std::string::npos) {
                break;
            }

            CurrentStart = TokenEnd + 1;
        }

        return true;
    }

    bool TryParseTerrainSelector(const std::string& Selector, TerrainSelectorData& OutData) {
        constexpr const char* Prefix{ "terrain:" };
        if (Selector.rfind(Prefix, 0) != 0) {
            return false;
        }

        const std::string ParameterText{ Selector.substr(8) };
        if (ParameterText.empty()) {
            return false;
        }

        std::size_t CurrentStart{ 0 };
        while (CurrentStart < ParameterText.size()) {
            const std::size_t TokenEnd{ ParameterText.find(';', CurrentStart) };
            const std::size_t TokenLength{ TokenEnd == std::string::npos ? ParameterText.size() - CurrentStart : TokenEnd - CurrentStart };
            const std::string Token{ ParameterText.substr(CurrentStart, TokenLength) };
            const std::size_t EqualsIndex{ Token.find('=') };
            if (EqualsIndex == std::string::npos || EqualsIndex == 0 || EqualsIndex + 1 >= Token.size()) {
                return false;
            }

            const std::string Key{ Token.substr(0, EqualsIndex) };
            const std::string Value{ Token.substr(EqualsIndex + 1) };
            if (Key == "HeightSourceType") {
                if (TryParseTerrainHeightSourceTypeText(Value, OutData.Desc.mHeightSourceType) == false) {
                    return false;
                }
            }
            else if (Key == "HeightMapPath") {
                OutData.Desc.HeightMapPath = Value;
            }
            else if (Key == "ProceduralHeightFieldPath") {
                OutData.Desc.mProceduralHeightFieldPath = Value;
                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralWidth") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mWidth) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralHeight") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mHeight) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralSeed") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mSeed) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralOctaveCount") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mOctaveCount) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralNoiseScale") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mNoiseScale) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralPersistence") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mPersistence) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralLacunarity") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mLacunarity) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralBaseHeight") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mBaseHeight) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralHeightAmplitude") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mHeightAmplitude) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralSmoothingPassCount") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mSmoothingPassCount) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralMinimumWidth") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mMinimumWidth) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralMinimumHeight") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mMinimumHeight) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralMaximumOctaveCount") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mMaximumOctaveCount) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralMaximumSmoothingPassCount") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mMaximumSmoothingPassCount) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralMinimumHeightValue") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mMinimumHeightValue) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralMaximumHeightValue") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mMaximumHeightValue) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralSampleScaleX") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mSampleScaleX) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralSampleScaleZ") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mSampleScaleZ) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralInitialFrequency") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mInitialFrequency) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralInitialAmplitude") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mInitialAmplitude) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralOctaveSeedStep") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mOctaveSeedStep) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralNoiseNormalizationScale") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mNoiseNormalizationScale) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralNoiseNormalizationBias") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mNoiseNormalizationBias) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralHashShiftA") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mHashShiftA) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralHashShiftB") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mHashShiftB) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralHashShiftC") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mHashShiftC) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralHashShiftLimitExclusive") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mHashShiftLimitExclusive) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralHashMultiplierA") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mHashMultiplierA) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralHashMultiplierB") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mHashMultiplierB) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralHashCoordinateOffsetX") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mHashCoordinateOffsetX) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralHashCoordinateOffsetZ") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mHashCoordinateOffsetZ) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralGradientDirectionCount") {
                if (TryParseUInt32(Value, OutData.Desc.mProceduralHeightFieldDesc.mGradientDirectionCount) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralFadeCoefficientA") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mFadeCoefficientA) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralFadeCoefficientB") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mFadeCoefficientB) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralFadeCoefficientC") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mFadeCoefficientC) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralSmoothingCornerWeight") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mSmoothingCornerWeight) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralSmoothingEdgeWeight") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mSmoothingEdgeWeight) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralSmoothingCenterWeight") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mSmoothingCenterWeight) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "ProceduralSmoothingWeightSum") {
                if (TryParseFloat(Value, OutData.Desc.mProceduralHeightFieldDesc.mSmoothingWeightSum) == false) {
                    return false;
                }

                OutData.Desc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
            else if (Key == "MaxHeight") {
                if (TryParseFloat(Value, OutData.Desc.MaxHeight) == false) {
                    return false;
                }
            }
            else if (Key == "CellSizeX") {
                if (TryParseFloat(Value, OutData.Desc.CellSizeX) == false) {
                    return false;
                }
            }
            else if (Key == "CellSizeZ") {
                if (TryParseFloat(Value, OutData.Desc.CellSizeZ) == false) {
                    return false;
                }
            }
            else if (Key == "FlipV") {
                if (TryParseBool(Value, OutData.Desc.FlipV) == false) {
                    return false;
                }
            }
            else if (Key == "CenterOrigin") {
                if (TryParseBool(Value, OutData.Desc.CenterOrigin) == false) {
                    return false;
                }
            }
            else if (Key == "TileQuadCount") {
                if (TryParseUInt32(Value, OutData.Desc.TileQuadCount) == false) {
                    return false;
                }
            }
            else if (Key == "LodCount") {
                if (TryParseUInt32(Value, OutData.Desc.LodCount) == false) {
                    return false;
                }
            }
            else if (Key == "LodDistances") {
                std::vector<float> LodDistances{};
                if (TryParseFloatList(Value, LodDistances) == false) {
                    return false;
                }

                OutData.Desc.LodDistances = std::move(LodDistances);
            }
            else {
                return false;
            }

            if (TokenEnd == std::string::npos) {
                break;
            }

            CurrentStart = TokenEnd + 1;
        }

        if (OutData.Desc.mHeightSourceType == Game::TerrainHeightSourceType::Procedural) {
            return true;
        }

        return OutData.Desc.HeightMapPath.empty() == false;
    }

    bool TryCreateTerrainMeshData(const std::string& Selector, MeshData& OutData) {
        TerrainSelectorData TerrainData{};
        if (TryParseTerrainSelector(Selector, TerrainData) == false) {
            return false;
        }

        try {
            Game::TerrainHeightFieldFactory HeightFieldFactory{};
            Game::TerrainMeshBuilder Builder{};
            const Game::HeightFieldData HeightField{ HeightFieldFactory.Build(TerrainData.Desc) };
            Game::TerrainMeshData TerrainMesh{ Builder.Build(HeightField, TerrainData.Desc) };
            OutData.Vertices = std::move(TerrainMesh.Vertices);
            OutData.Indices = std::move(TerrainMesh.Indices);
            return true;
        }
        catch (const std::exception&) {
            return false;
        }
    }

    bool TryCreateMeshDataBySelector(const std::string& Selector, MeshData& OutData) {
        if (Selector.rfind("terrain:", 0) == 0) {
            return TryCreateTerrainMeshData(Selector, OutData);
        }

        PrimitiveParameters Parameters{};
        std::string PrimitiveType{};
        const bool IsValidSelector{ TryParseSelector(Selector, PrimitiveType, Parameters) };
        if (IsValidSelector == false) {
            return false;
        }

        if (PrimitiveType == "primitive:box" || PrimitiveType == "box") {
            OutData = CreateBox(Parameters);
            return true;
        }

        if (PrimitiveType == "primitive:sphere" || PrimitiveType == "sphere") {
            OutData = CreateSphere(Parameters);
            return true;
        }

        if (PrimitiveType == "primitive:cylinder" || PrimitiveType == "cylinder") {
            OutData = CreateCylinder(Parameters);
            return true;
        }

        if (PrimitiveType == "primitive:cone" || PrimitiveType == "cone") {
            OutData = CreateCone(Parameters);
            return true;
        }

        if (PrimitiveType == "primitive:triangular_pyramid" || PrimitiveType == "triangular_pyramid") {
            OutData = CreateTriangularPyramid(Parameters);
            return true;
        }

        if (PrimitiveType == "primitive:square_pyramid" || PrimitiveType == "square_pyramid") {
            OutData = CreateSquarePyramid(Parameters);
            return true;
        }

        if (PrimitiveType == "primitive:sky_dome" || PrimitiveType == "sky_dome" || PrimitiveType == "primitive:skydome" || PrimitiveType == "skydome") {
            OutData = CreateSkyDome(Parameters);
            return true;
        }

        return false;
    }
}

namespace Game {
    bool PrimitiveModelFactory::TryCreateModelResult(const std::string& Selector, asset::ModelResult& OutModelData) {
        MeshData Data{};
        if (TryCreateMeshDataBySelector(Selector, Data) == false) {
            return false;
        }

        OutModelData = asset::ModelResult{};
        asset::ModelNode& RootNode{ OutModelData.CreateNode(Selector, nullptr) };
        RootNode.Vertices() = std::move(Data.Vertices);
        RootNode.Indices() = std::move(Data.Indices);

        asset::ModelNode::SubMesh SubMesh{};
        SubMesh.IndexOffset = 0;
        SubMesh.IndexCount = RootNode.Indices().size();
        SubMesh.MaterialGroupItemIndex = 0;
        RootNode.SubMeshes().push_back(SubMesh);

        return true;
    }
}
