#include "PrimitiveModelFactory.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

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

    asset::Vec3 ScalePosition(const asset::Vec3& Position, float Size) {
        return asset::Vec3{ Position.mX * Size, Position.mY * Size, Position.mZ * Size };
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
            AddTriangle(Data, Base + 0, Base + 2, Base + 1);
            AddTriangle(Data, Base + 0, Base + 3, Base + 2);
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
                const asset::Vec3 Position{ Radius * Normal.mX, Radius * Normal.mY, Radius * Normal.mZ };
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
                AddTriangle(Data, I0, I2, I1);
                AddTriangle(Data, I1, I2, I3);
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
            const asset::Vec3 ToApex{ Apex.mX - BasePoint.mX, Apex.mY - BasePoint.mY, Apex.mZ - BasePoint.mZ };
            const asset::Vec3 Normal{ ToApex.mY * Tangent.mZ - ToApex.mZ * Tangent.mY, ToApex.mZ * Tangent.mX - ToApex.mX * Tangent.mZ, ToApex.mX * Tangent.mY - ToApex.mY * Tangent.mX };
            const float Length{ std::sqrt(Normal.mX * Normal.mX + Normal.mY * Normal.mY + Normal.mZ * Normal.mZ) };
            const asset::Vec3 UnitNormal{ Normal.mX / Length, Normal.mY / Length, Normal.mZ / Length };
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
            const asset::Vec3 Edge0{ Face[1].mX - Face[0].mX, Face[1].mY - Face[0].mY, Face[1].mZ - Face[0].mZ };
            const asset::Vec3 Edge1{ Face[2].mX - Face[0].mX, Face[2].mY - Face[0].mY, Face[2].mZ - Face[0].mZ };
            const asset::Vec3 NormalRaw{ Edge0.mY * Edge1.mZ - Edge0.mZ * Edge1.mY, Edge0.mZ * Edge1.mX - Edge0.mX * Edge1.mZ, Edge0.mX * Edge1.mY - Edge0.mY * Edge1.mX };
            const float Length{ std::sqrt(NormalRaw.mX * NormalRaw.mX + NormalRaw.mY * NormalRaw.mY + NormalRaw.mZ * NormalRaw.mZ) };
            const asset::Vec3 Normal{ NormalRaw.mX / Length, NormalRaw.mY / Length, NormalRaw.mZ / Length };
            const std::uint32_t BaseIndex{ static_cast<std::uint32_t>(Data.Vertices.VertexCount()) };
            AddVertex(Data, Face[0], Normal, FaceUv[0], Parameters.Color);
            AddVertex(Data, Face[1], Normal, FaceUv[1], Parameters.Color);
            AddVertex(Data, Face[2], Normal, FaceUv[2], Parameters.Color);
            AddTriangle(Data, BaseIndex + 0, BaseIndex + 2, BaseIndex + 1);
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
            const asset::Vec3 Edge0{ Face[1].mX - Face[0].mX, Face[1].mY - Face[0].mY, Face[1].mZ - Face[0].mZ };
            const asset::Vec3 Edge1{ Face[2].mX - Face[0].mX, Face[2].mY - Face[0].mY, Face[2].mZ - Face[0].mZ };
            const asset::Vec3 NormalRaw{ Edge0.mY * Edge1.mZ - Edge0.mZ * Edge1.mY, Edge0.mZ * Edge1.mX - Edge0.mX * Edge1.mZ, Edge0.mX * Edge1.mY - Edge0.mY * Edge1.mX };
            const float Length{ std::sqrt(NormalRaw.mX * NormalRaw.mX + NormalRaw.mY * NormalRaw.mY + NormalRaw.mZ * NormalRaw.mZ) };
            const asset::Vec3 Normal{ NormalRaw.mX / Length, NormalRaw.mY / Length, NormalRaw.mZ / Length };
            const std::uint32_t BaseIndex{ static_cast<std::uint32_t>(Data.Vertices.VertexCount()) };
            AddVertex(Data, Face[0], Normal, FaceUv[0], Parameters.Color);
            AddVertex(Data, Face[1], Normal, FaceUv[1], Parameters.Color);
            AddVertex(Data, Face[2], Normal, FaceUv[2], Parameters.Color);
            AddTriangle(Data, BaseIndex + 0, BaseIndex + 2, BaseIndex + 1);
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

    bool TryCreateMeshDataBySelector(const std::string& Selector, MeshData& OutData) {
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
