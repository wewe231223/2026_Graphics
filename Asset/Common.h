#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Utility/DirectXInclude.h"

namespace asset {
    using Vec2 = DirectX::SimpleMath::Vector2;
    using Vec3 = DirectX::SimpleMath::Vector3;
    using Vec4 = DirectX::SimpleMath::Vector4;
    using Mat4 = DirectX::SimpleMath::Matrix;
    using UVec4 = std::array<std::uint32_t, 4>;

    enum class GraphicsAPI : std::uint8_t {
        DirectX,
        OpenGL,
    };

    struct VertexAttributes final {
    public:
        std::vector<Vec3> Positions{};
        std::vector<Vec3> Normals{};
        std::array<std::vector<Vec2>, 4> TexCoords{};
        std::vector<Vec4> Colors{};
        std::vector<Vec3> Tangents{};
        std::vector<Vec3> Bitangents{};
        std::vector<UVec4> BoneIndices{};
        std::vector<Vec4> BoneWeights{};

    public:
        std::size_t VertexCount() const;
        bool Empty() const;

        void Reserve(std::size_t Count);
        void Resize(std::size_t Count);
    };

    enum class MaterialType : std::uint16_t {
        ShadingModel = 0,
        TwoSided = 1,
        Wireframe = 2,
        BlendMode = 3,
        Opacity = 4,
        AlphaMode = 5,
        AlphaCutoff = 6,
        BaseColor = 7,
        DiffuseColor = 8,
        AmbientColor = 9,
        SpecularColor = 10,
        EmissiveColor = 11,
        TransparentColor = 12,
        ReflectiveColor = 13,
        MetallicFactor = 14,
        RoughnessFactor = 15,
        NormalScale = 16,
        OcclusionStrength = 17,
        EmissiveStrength = 18,
        DiffuseTexture = 19,
        SpecularTexture = 20,
        AmbientTexture = 21,
        EmissiveTexture = 22,
        OpacityTexture = 23,
        ShininessTexture = 24,
        HeightBumpTexture = 25,
        NormalTexture = 26,
        DisplacementTexture = 27,
        ReflectionTexture = 28,
        LightmapTexture = 29
    };

    inline constexpr std::uint32_t MaterialTypeCount{ 30 };

    enum class MaterialMapKind : std::uint8_t {
        None,
        Real,
        Vec2,
        Vec3,
        Vec4,
        Int,
        Bool,
        String,
    };

    class MaterialMap final {
    public:
        MaterialMap();
        explicit MaterialMap(float Value);
        explicit MaterialMap(std::int64_t Value);
        explicit MaterialMap(bool Value);
        explicit MaterialMap(const Vec2& Value);
        explicit MaterialMap(const Vec3& Value);
        explicit MaterialMap(const Vec4& Value);
        explicit MaterialMap(std::string Value);

    public:
        MaterialMapKind GetKind() const;
        float GetReal() const;
        std::int64_t GetInt() const;
        bool GetBool() const;
        Vec2 GetVec2() const;
        Vec3 GetVec3() const;
        Vec4 GetVec4() const;
        const std::string& GetString() const;

    private:
        MaterialMapKind mKind{ MaterialMapKind::None };
        union {
            float mReal;
            std::int64_t mInt;
            bool mBool;
            float mVec2[2];
            float mVec3[3];
            float mVec4[4];
        } mValue{};
        std::string mString{};
    };

    struct MaterialProperty final {
    public:
        MaterialType Type{};
        MaterialMap Data{};
    };

    struct Material final {
    public:
        std::string Name{};
        std::vector<MaterialProperty> Properties{};
        bool PBR{ false };
    };

    struct MaterialGroupItem final {
    public:
        Material MaterialData{};
        std::string PipelineName{};
    };

    struct MaterialGroup final {
    public:
        std::string Name{};
        std::vector<MaterialGroupItem> Items{};
    };

    struct AssetError final : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };
}
