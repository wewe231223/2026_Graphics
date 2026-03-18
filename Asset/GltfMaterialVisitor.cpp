#include "GltfMaterialVisitor.h"

#include <string>
#include <string_view>

namespace asset {
    namespace {
        std::string ToString(const char* Value) {
            if (Value == nullptr) {
                return std::string{};
            }

            return std::string{ Value };
        }

        std::string ExtractFileName(std::string_view Path) {
            const std::size_t Position{ Path.find_last_of("/\\") };
            if (Position == std::string_view::npos) {
                return std::string{ Path };
            }

            return std::string{ Path.substr(Position + 1) };
        }

        std::string GetImageFileName(const cgltf_image* Image) {
            if (Image == nullptr) {
                return std::string{};
            }

            std::string FileName{ ToString(Image->uri) };
            if (FileName.empty()) {
                FileName = ToString(Image->name);
            }

            if (FileName.empty()) {
                return std::string{};
            }

            return ExtractFileName(FileName);
        }

        void AppendTextureProperty(const cgltf_texture* Texture, MaterialType TextureType, Material& OutMaterial) {
            if (Texture == nullptr) {
                return;
            }

            const std::string FileName{ GetImageFileName(Texture->image) };
            if (FileName.empty()) {
                return;
            }

            OutMaterial.Properties.push_back(MaterialProperty{ TextureType, MaterialMap{ FileName } });
        }

        void AppendTextureProperty(const cgltf_texture_view& TextureView, MaterialType TextureType, Material& OutMaterial) {
            AppendTextureProperty(TextureView.texture, TextureType, OutMaterial);
        }

        void AppendFloatProperty(float Value, MaterialType ValueType, Material& OutMaterial) {
            OutMaterial.Properties.push_back(MaterialProperty{ ValueType, MaterialMap{ Value } });
        }

        void AppendVec3Property(const float* Value, MaterialType ValueType, Material& OutMaterial) {
            OutMaterial.Properties.push_back(MaterialProperty{ ValueType, MaterialMap{ Vec3{ Value[0], Value[1], Value[2] } } });
        }

        void AppendVec4Property(const float* Value, MaterialType ValueType, Material& OutMaterial) {
            OutMaterial.Properties.push_back(MaterialProperty{ ValueType, MaterialMap{ Vec4{ Value[0], Value[1], Value[2], Value[3] } } });
        }

        Material BuildMaterial(const cgltf_material& MaterialData) {
            Material OutMaterial{};
            OutMaterial.Name = ToString(MaterialData.name);
            OutMaterial.PBR = true;

            if (MaterialData.has_pbr_metallic_roughness != 0) {
                AppendVec4Property(MaterialData.pbr_metallic_roughness.base_color_factor, MaterialType::BaseColor, OutMaterial);
                AppendFloatProperty(MaterialData.pbr_metallic_roughness.metallic_factor, MaterialType::Metalness, OutMaterial);
                AppendFloatProperty(MaterialData.pbr_metallic_roughness.roughness_factor, MaterialType::Roughness, OutMaterial);
                AppendTextureProperty(MaterialData.pbr_metallic_roughness.base_color_texture, MaterialType::BaseColorMap, OutMaterial);
                AppendTextureProperty(MaterialData.pbr_metallic_roughness.metallic_roughness_texture, MaterialType::MetalnessMap, OutMaterial);
                AppendTextureProperty(MaterialData.pbr_metallic_roughness.metallic_roughness_texture, MaterialType::RoughnessMap, OutMaterial);
            }

            AppendVec3Property(MaterialData.emissive_factor, MaterialType::EmissionColorPbr, OutMaterial);
            AppendTextureProperty(MaterialData.emissive_texture, MaterialType::EmissionColorPbrMap, OutMaterial);
            AppendTextureProperty(MaterialData.normal_texture.texture, MaterialType::NormalMapPbrMap, OutMaterial);
            AppendTextureProperty(MaterialData.occlusion_texture.texture, MaterialType::AmbientOcclusionMap, OutMaterial);

            if (MaterialData.normal_texture.texture != nullptr) {
                AppendFloatProperty(MaterialData.normal_texture.scale, MaterialType::BumpFactor, OutMaterial);
            }

            if (MaterialData.occlusion_texture.texture != nullptr) {
                AppendFloatProperty(MaterialData.occlusion_texture.scale, MaterialType::AmbientOcclusion, OutMaterial);
            }

            if (MaterialData.alpha_mode == cgltf_alpha_mode_blend) {
                AppendFloatProperty(1.0f, MaterialType::Opacity, OutMaterial);
            }
            else if (MaterialData.alpha_mode == cgltf_alpha_mode_mask) {
                AppendFloatProperty(MaterialData.alpha_cutoff, MaterialType::Opacity, OutMaterial);
            }

            return OutMaterial;
        }
    }

    GltfMaterialVisitor::GltfMaterialVisitor() = default;

    GltfMaterialVisitor::~GltfMaterialVisitor() = default;

    void GltfMaterialVisitor::OnNodeBegin(const cgltf_data& Scene, const cgltf_node& Node, const GltfNodeVisitContext& Context) {
        static_cast<void>(Scene);
        static_cast<void>(Context);

        if (Node.mesh == nullptr) {
            return;
        }

        for (cgltf_size PrimitiveIndex{ 0 }; PrimitiveIndex < Node.mesh->primitives_count; ++PrimitiveIndex) {
            const cgltf_primitive& Primitive{ Node.mesh->primitives[PrimitiveIndex] };
            const cgltf_material* const MaterialData{ Primitive.material };
            if (MaterialData == nullptr) {
                continue;
            }

            if (mVisitedMaterials.find(MaterialData) != mVisitedMaterials.end()) {
                continue;
            }

            mVisitedMaterials.insert(MaterialData);
            const std::size_t MaterialIndex{ mMaterials.size() };
            mMaterials.push_back(BuildMaterial(*MaterialData));
            mMaterialLookup.insert({ MaterialData, MaterialIndex });
        }
    }

    void GltfMaterialVisitor::OnNodeEnd(const cgltf_data& Scene, const cgltf_node& Node) {
        static_cast<void>(Scene);
        static_cast<void>(Node);
    }

    std::vector<Material>& GltfMaterialVisitor::GetMaterials() {
        return mMaterials;
    }

    const std::unordered_map<const cgltf_material*, std::size_t>& GltfMaterialVisitor::GetMaterialLookup() const {
        return mMaterialLookup;
    }

    void GltfMaterialVisitor::Clear() {
        mMaterials.clear();
        mMaterialLookup.clear();
        mVisitedMaterials.clear();
    }
}
