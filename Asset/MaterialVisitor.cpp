#include "MaterialVisitor.h"

#include <optional>
#include <string>
#include <string_view>

using namespace asset;

namespace {
    Vec2 ToVec2(const ufbx_vec2& Value) {
        return Vec2{ static_cast<float>(Value.x), static_cast<float>(Value.y) };
    }

    Vec3 ToVec3(const ufbx_vec3& Value) {
        return Vec3{ static_cast<float>(Value.x), static_cast<float>(Value.y), static_cast<float>(Value.z) };
    }

    Vec4 ToVec4(const ufbx_vec4& Value) {
        return Vec4{ static_cast<float>(Value.x), static_cast<float>(Value.y), static_cast<float>(Value.z), static_cast<float>(Value.w) };
    }

    std::string ToString(const ufbx_string& Value) {
        if (Value.data == nullptr || Value.length == 0) {
            return std::string{};
        }
        return std::string{ Value.data, Value.length };
    }

    std::string ExtractFileName(std::string_view Path) {
        std::size_t Position{ Path.find_last_of("/\\") };
        if (Position == std::string_view::npos) {
            return std::string{ Path };
        }
        return std::string{ Path.substr(Position + 1) };
    }

    std::string GetTextureFileName(const ufbx_texture& Texture) {
        std::string FileName{ ToString(Texture.filename) };
        if (FileName.empty()) {
            FileName = ToString(Texture.relative_filename);
        }
        if (FileName.empty()) {
            FileName = ToString(Texture.absolute_filename);
        }
        if (FileName.empty()) {
            return std::string{};
        }
        return ExtractFileName(FileName);
    }

    std::optional<MaterialMap> CreateValueMaterialMap(const ufbx_material_map& Map) {
        if (!Map.has_value) {
            return std::nullopt;
        }
        if (Map.value_components == 1) {
            return MaterialMap{ static_cast<float>(Map.value_real) };
        }
        if (Map.value_components == 2) {
            return MaterialMap{ ToVec2(Map.value_vec2) };
        }
        if (Map.value_components == 3) {
            return MaterialMap{ ToVec3(Map.value_vec3) };
        }
        if (Map.value_components == 4) {
            return MaterialMap{ ToVec4(Map.value_vec4) };
        }
        return MaterialMap{ static_cast<std::int64_t>(Map.value_int) };
    }

    void AppendMaterialMap(const ufbx_material_map& Map, MaterialType ValueType, MaterialType TextureType, Material& OutMaterial) {
        std::optional<MaterialMap> ValueMap{ CreateValueMaterialMap(Map) };
        if (ValueMap.has_value()) {
            OutMaterial.Properties.push_back(MaterialProperty{ ValueType, ValueMap.value() });
        }
        if (Map.texture != nullptr && Map.texture_enabled) {
            std::string FileName{ GetTextureFileName(*Map.texture) };
            if (!FileName.empty()) {
                OutMaterial.Properties.push_back(MaterialProperty{ TextureType, MaterialMap{ std::move(FileName) } });
            }
        }
    }

    Material BuildMaterial(const ufbx_material& MaterialData) {
        Material OutMaterial{};

        if (MaterialData.features.pbr.enabled) {
			OutMaterial.PBR = true;
            AppendMaterialMap(MaterialData.pbr.base_factor, MaterialType::BaseFactor, MaterialType::BaseColorMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.base_color, MaterialType::BaseColor, MaterialType::BaseColorMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.roughness, MaterialType::Roughness, MaterialType::RoughnessMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.metalness, MaterialType::Metalness, MaterialType::MetalnessMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.diffuse_roughness, MaterialType::DiffuseRoughness, MaterialType::DiffuseRoughnessMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.specular_factor, MaterialType::SpecularFactorPbr, MaterialType::SpecularFactorPbrMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.specular_color, MaterialType::SpecularColorPbr, MaterialType::SpecularColorPbrMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.specular_ior, MaterialType::SpecularIor, MaterialType::SpecularIorMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.specular_anisotropy, MaterialType::SpecularAnisotropy, MaterialType::SpecularAnisotropyMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.specular_rotation, MaterialType::SpecularRotation, MaterialType::SpecularRotationMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.transmission_factor, MaterialType::TransmissionFactor, MaterialType::TransmissionFactorMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.transmission_color, MaterialType::TransmissionColor, MaterialType::TransmissionColorMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.transmission_depth, MaterialType::TransmissionDepth, MaterialType::TransmissionDepthMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.transmission_scatter, MaterialType::TransmissionScatter, MaterialType::TransmissionScatterMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.transmission_scatter_anisotropy, MaterialType::TransmissionScatterAnisotropy, MaterialType::TransmissionScatterAnisotropyMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.transmission_dispersion, MaterialType::TransmissionDispersion, MaterialType::TransmissionDispersionMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.transmission_roughness, MaterialType::TransmissionRoughness, MaterialType::TransmissionRoughnessMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.transmission_extra_roughness, MaterialType::TransmissionExtraRoughness, MaterialType::TransmissionExtraRoughnessMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.transmission_priority, MaterialType::TransmissionPriority, MaterialType::TransmissionPriorityMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.transmission_enable_in_aov, MaterialType::TransmissionEnableInAov, MaterialType::TransmissionEnableInAovMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.subsurface_factor, MaterialType::SubsurfaceFactor, MaterialType::SubsurfaceFactorMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.subsurface_color, MaterialType::SubsurfaceColor, MaterialType::SubsurfaceColorMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.subsurface_radius, MaterialType::SubsurfaceRadius, MaterialType::SubsurfaceRadiusMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.subsurface_scale, MaterialType::SubsurfaceScale, MaterialType::SubsurfaceScaleMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.subsurface_anisotropy, MaterialType::SubsurfaceAnisotropy, MaterialType::SubsurfaceAnisotropyMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.subsurface_tint_color, MaterialType::SubsurfaceTintColor, MaterialType::SubsurfaceTintColorMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.subsurface_type, MaterialType::SubsurfaceType, MaterialType::SubsurfaceTypeMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.sheen_factor, MaterialType::SheenFactor, MaterialType::SheenFactorMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.sheen_color, MaterialType::SheenColor, MaterialType::SheenColorMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.sheen_roughness, MaterialType::SheenRoughness, MaterialType::SheenRoughnessMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.coat_factor, MaterialType::CoatFactor, MaterialType::CoatFactorMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.coat_color, MaterialType::CoatColor, MaterialType::CoatColorMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.coat_roughness, MaterialType::CoatRoughness, MaterialType::CoatRoughnessMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.coat_ior, MaterialType::CoatIor, MaterialType::CoatIorMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.coat_anisotropy, MaterialType::CoatAnisotropy, MaterialType::CoatAnisotropyMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.coat_rotation, MaterialType::CoatRotation, MaterialType::CoatRotationMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.coat_normal, MaterialType::CoatNormal, MaterialType::CoatNormalMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.coat_affect_base_color, MaterialType::CoatAffectBaseColor, MaterialType::CoatAffectBaseColorMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.coat_affect_base_roughness, MaterialType::CoatAffectBaseRoughness, MaterialType::CoatAffectBaseRoughnessMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.thin_film_factor, MaterialType::ThinFilmFactor, MaterialType::ThinFilmFactorMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.thin_film_thickness, MaterialType::ThinFilmThickness, MaterialType::ThinFilmThicknessMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.thin_film_ior, MaterialType::ThinFilmIor, MaterialType::ThinFilmIorMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.emission_factor, MaterialType::EmissionFactorPbr, MaterialType::EmissionFactorPbrMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.emission_color, MaterialType::EmissionColorPbr, MaterialType::EmissionColorPbrMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.opacity, MaterialType::Opacity, MaterialType::OpacityMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.indirect_diffuse, MaterialType::IndirectDiffuse, MaterialType::IndirectDiffuseMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.indirect_specular, MaterialType::IndirectSpecular, MaterialType::IndirectSpecularMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.normal_map, MaterialType::NormalMapPbr, MaterialType::NormalMapPbrMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.tangent_map, MaterialType::TangentMap, MaterialType::TangentMapMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.displacement_map, MaterialType::DisplacementMapPbr, MaterialType::DisplacementMapPbrMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.matte_factor, MaterialType::MatteFactor, MaterialType::MatteFactorMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.matte_color, MaterialType::MatteColor, MaterialType::MatteColorMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.ambient_occlusion, MaterialType::AmbientOcclusion, MaterialType::AmbientOcclusionMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.glossiness, MaterialType::Glossiness, MaterialType::GlossinessMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.coat_glossiness, MaterialType::CoatGlossiness, MaterialType::CoatGlossinessMap, OutMaterial);
            AppendMaterialMap(MaterialData.pbr.transmission_glossiness, MaterialType::TransmissionGlossiness, MaterialType::TransmissionGlossinessMap, OutMaterial);
        } else {
            AppendMaterialMap(MaterialData.fbx.diffuse_factor, MaterialType::DiffuseFactor, MaterialType::DiffuseFactorMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.diffuse_color, MaterialType::DiffuseColor, MaterialType::DiffuseColorMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.specular_factor, MaterialType::SpecularFactor, MaterialType::SpecularFactorMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.specular_color, MaterialType::SpecularColor, MaterialType::SpecularColorMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.specular_exponent, MaterialType::SpecularExponent, MaterialType::SpecularExponentMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.reflection_factor, MaterialType::ReflectionFactor, MaterialType::ReflectionFactorMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.reflection_color, MaterialType::ReflectionColor, MaterialType::ReflectionColorMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.transparency_factor, MaterialType::TransparencyFactor, MaterialType::TransparencyFactorMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.transparency_color, MaterialType::TransparencyColor, MaterialType::TransparencyColorMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.emission_factor, MaterialType::EmissionFactor, MaterialType::EmissionFactorMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.emission_color, MaterialType::EmissionColor, MaterialType::EmissionColorMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.ambient_factor, MaterialType::AmbientFactor, MaterialType::AmbientFactorMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.ambient_color, MaterialType::AmbientColor, MaterialType::AmbientColorMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.normal_map, MaterialType::NormalMap, MaterialType::NormalMapMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.bump, MaterialType::Bump, MaterialType::BumpMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.bump_factor, MaterialType::BumpFactor, MaterialType::BumpFactorMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.displacement_factor, MaterialType::DisplacementFactor, MaterialType::DisplacementFactorMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.displacement, MaterialType::Displacement, MaterialType::DisplacementMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.vector_displacement_factor, MaterialType::VectorDisplacementFactor, MaterialType::VectorDisplacementFactorMap, OutMaterial);
            AppendMaterialMap(MaterialData.fbx.vector_displacement, MaterialType::VectorDisplacement, MaterialType::VectorDisplacementMap, OutMaterial);
        }
        return OutMaterial;
    }
}

MaterialVisitor::MaterialVisitor() = default;

MaterialVisitor::~MaterialVisitor() = default;

void MaterialVisitor::OnNodeBegin(const ufbx_scene& Scene, const ufbx_node& Node, const NodeVisitContext& Context) {
    static_cast<void>(Scene);
    static_cast<void>(Context);
    std::size_t Count{ Node.materials.count };
    for (std::size_t Index{ 0 }; Index < Count; ++Index) {
        const ufbx_material* MaterialData{ Node.materials.data[Index] };
        if (MaterialData == nullptr) {
            continue;
        }
        if (mVisitedMaterials.find(MaterialData) != mVisitedMaterials.end()) {
            continue;
        }
        mVisitedMaterials.insert(MaterialData);
        std::size_t MaterialIndex{ mMaterials.size() };
        mMaterials.push_back(BuildMaterial(*MaterialData));
        mMaterialLookup.insert({ MaterialData, MaterialIndex });
    }
}

void MaterialVisitor::OnNodeEnd(const ufbx_scene& Scene, const ufbx_node& Node) {
    static_cast<void>(Scene);
    static_cast<void>(Node);
}

std::vector<Material>& MaterialVisitor::GetMaterials() {
    return mMaterials;
}

const std::unordered_map<const ufbx_material*, std::size_t>& MaterialVisitor::GetMaterialLookup() const {
    return mMaterialLookup;
}

void MaterialVisitor::Clear() {
    mMaterials.clear();
    mMaterialLookup.clear();
    mVisitedMaterials.clear();
}
