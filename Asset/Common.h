#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <functional>
#include <stdexcept>
#include <utility>
#include <type_traits>
#include <new>
#include <cassert>

#include "ufbx.h"
#include "NumericTypes.h"

namespace asset {
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

        std::size_t VertexCount() const;
        bool Empty() const;

        void Reserve(std::size_t Count);
        void Resize(std::size_t Count);
    };


    enum class MaterialType : std::uint16_t {
        // FBX 
        DiffuseFactor                        = 0,
        DiffuseFactorMap                     = 1,
        DiffuseColor                         = 2,
        DiffuseColorMap                      = 3,
        SpecularFactor                       = 4,
        SpecularFactorMap                    = 5,
        SpecularColor                        = 6,
        SpecularColorMap                     = 7,
        SpecularExponent                     = 8,
        SpecularExponentMap                  = 9,
        ReflectionFactor                     = 10,
        ReflectionFactorMap                  = 11,
        ReflectionColor                      = 12,
        ReflectionColorMap                   = 13,
        TransparencyFactor                   = 14,
        TransparencyFactorMap                = 15,
        TransparencyColor                    = 16,
        TransparencyColorMap                 = 17,
        EmissionFactor                       = 18,
        EmissionFactorMap                    = 19,
        EmissionColor                        = 20,
        EmissionColorMap                     = 21,
        AmbientFactor                        = 22,
        AmbientFactorMap                     = 23,
        AmbientColor                         = 24,
        AmbientColorMap                      = 25,
        NormalMap                            = 26,
        NormalMapMap                         = 27,
        Bump                                 = 28,
        BumpMap                              = 29,
        BumpFactor                           = 30,
        BumpFactorMap                        = 31,
        DisplacementFactor                   = 32,
        DisplacementFactorMap                = 33,
        Displacement                         = 34,
        DisplacementMap                      = 35,
        VectorDisplacementFactor             = 36,
        VectorDisplacementFactorMap          = 37,
        VectorDisplacement                   = 38,
        VectorDisplacementMap                = 39,

        // PBR 
        BaseFactor                           = 40,
        BaseFactorMap                        = 41,
        BaseColor                            = 42,
        BaseColorMap                         = 43,
        Roughness                            = 44,
        RoughnessMap                         = 45,
        Metalness                            = 46,
        MetalnessMap                         = 47,
        DiffuseRoughness                     = 48,
        DiffuseRoughnessMap                  = 49,
        SpecularFactorPbr                    = 50,
        SpecularFactorPbrMap                 = 51,
        SpecularColorPbr                     = 52,
        SpecularColorPbrMap                  = 53,
        SpecularIor                          = 54,
        SpecularIorMap                       = 55,
        SpecularAnisotropy                   = 56,
        SpecularAnisotropyMap                = 57,
        SpecularRotation                     = 58,
        SpecularRotationMap                  = 59,
        TransmissionFactor                   = 60,
        TransmissionFactorMap                = 61,
        TransmissionColor                    = 62,
        TransmissionColorMap                 = 63,
        TransmissionDepth                    = 64,
        TransmissionDepthMap                 = 65,
        TransmissionScatter                  = 66,
        TransmissionScatterMap               = 67,
        TransmissionScatterAnisotropy        = 68,
        TransmissionScatterAnisotropyMap     = 69,
        TransmissionDispersion               = 70,
        TransmissionDispersionMap            = 71,
        TransmissionRoughness                = 72,
        TransmissionRoughnessMap             = 73,
        TransmissionExtraRoughness           = 74,
        TransmissionExtraRoughnessMap        = 75,
        TransmissionPriority                 = 76,
        TransmissionPriorityMap              = 77,
        TransmissionEnableInAov              = 78,
        TransmissionEnableInAovMap           = 79,
        SubsurfaceFactor                     = 80,
        SubsurfaceFactorMap                  = 81,
        SubsurfaceColor                      = 82,
        SubsurfaceColorMap                   = 83,
        SubsurfaceRadius                     = 84,
        SubsurfaceRadiusMap                  = 85,
        SubsurfaceScale                      = 86,
        SubsurfaceScaleMap                   = 87,
        SubsurfaceAnisotropy                 = 88,
        SubsurfaceAnisotropyMap              = 89,
        SubsurfaceTintColor                  = 90,
        SubsurfaceTintColorMap               = 91,
        SubsurfaceType                       = 92,
        SubsurfaceTypeMap                    = 93,
        SheenFactor                          = 94,
        SheenFactorMap                       = 95,
        SheenColor                           = 96,
        SheenColorMap                        = 97,
        SheenRoughness                       = 98,
        SheenRoughnessMap                    = 99,
        CoatFactor                           = 100,
        CoatFactorMap                        = 101,
        CoatColor                            = 102,
        CoatColorMap                         = 103,
        CoatRoughness                        = 104,
        CoatRoughnessMap                     = 105,
        CoatIor                              = 106,
        CoatIorMap                           = 107,
        CoatAnisotropy                       = 108,
        CoatAnisotropyMap                    = 109,
        CoatRotation                         = 110,
        CoatRotationMap                      = 111,
        CoatNormal                           = 112,
        CoatNormalMap                        = 113,
        CoatAffectBaseColor                  = 114,
        CoatAffectBaseColorMap               = 115,
        CoatAffectBaseRoughness              = 116,
        CoatAffectBaseRoughnessMap           = 117,
        ThinFilmFactor                       = 118,
        ThinFilmFactorMap                    = 119,
        ThinFilmThickness                    = 120,
        ThinFilmThicknessMap                 = 121,
        ThinFilmIor                          = 122,
        ThinFilmIorMap                       = 123,
        EmissionFactorPbr                    = 124,
        EmissionFactorPbrMap                 = 125,
        EmissionColorPbr                     = 126,
        EmissionColorPbrMap                  = 127,
        Opacity                              = 128,
        OpacityMap                           = 129,
        IndirectDiffuse                      = 130,
        IndirectDiffuseMap                   = 131,
        IndirectSpecular                     = 132,
        IndirectSpecularMap                  = 133,
        NormalMapPbr                         = 134,
        NormalMapPbrMap                      = 135,
        TangentMap                           = 136,
        TangentMapMap                        = 137,
        DisplacementMapPbr                   = 138,
        DisplacementMapPbrMap                = 139,
        MatteFactor                          = 140,
        MatteFactorMap                       = 141,
        MatteColor                           = 142,
        MatteColorMap                        = 143,
        AmbientOcclusion                     = 144,
        AmbientOcclusionMap                  = 145,
        Glossiness                           = 146,
        GlossinessMap                        = 147,
        CoatGlossiness                       = 148,
        CoatGlossinessMap                    = 149,
        TransmissionGlossiness               = 150,
        TransmissionGlossinessMap            = 151
    };

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

    struct MaterialMap final {
    public:
        MaterialMap();
        explicit MaterialMap(float Value);
        explicit MaterialMap(std::int64_t Value);
        explicit MaterialMap(bool Value);
        explicit MaterialMap(const Vec2& Value);
        explicit MaterialMap(const Vec3& Value);
        explicit MaterialMap(const Vec4& Value);
        explicit MaterialMap(std::string Value);

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
        } mValue;
        std::string mString{};
    };

    struct MaterialProperty {
		MaterialType Type;
        MaterialMap Data; 
    };

    struct Material {
		std::vector<MaterialProperty> Properties;
        bool PBR{ false };
    };


    struct AssetError final : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };
}
