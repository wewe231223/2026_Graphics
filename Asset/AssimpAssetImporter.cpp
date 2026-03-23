#include "AssimpAssetImporter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace asset {
    namespace {
        constexpr unsigned int MaxBoneInfluenceCount{ 4 };

        Vec3 ToVec3(const aiVector3D& Value) {
            return Vec3{ Value.x, Value.y, Value.z };
        }

        Vec2 ToVec2(const aiVector3D& Value, bool IsUvFlipEnabled) {
            return Vec2{ Value.x, IsUvFlipEnabled ? 1.0f - Value.y : Value.y };
        }

        Vec4 ToVec4(const aiColor4D& Value) {
            return Vec4{ Value.r, Value.g, Value.b, Value.a };
        }

        Mat4 ToMat4(const aiMatrix4x4& Value) {
            return Mat4{ Value.a1, Value.b1, Value.c1, Value.d1, Value.a2, Value.b2, Value.c2, Value.d2, Value.a3, Value.b3, Value.c3, Value.d3, Value.a4, Value.b4, Value.c4, Value.d4 };
        }

        void AppendProperty(Material& OutMaterial, MaterialType Type, const MaterialMap& Data) {
            OutMaterial.Properties.push_back(MaterialProperty{ Type, Data });
        }

        void AppendTextureProperty(const aiMaterial& MaterialData, aiTextureType TextureType, MaterialType Type, Material& OutMaterial) {
            aiString TexturePath{};
            if (MaterialData.GetTexture(TextureType, 0, &TexturePath) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, Type, MaterialMap{ std::string{ TexturePath.C_Str() } });
            }
        }

        void FillMaterial(const aiMaterial& MaterialData, Material& OutMaterial) {
            aiString MaterialName{};
            if (MaterialData.Get(AI_MATKEY_NAME, MaterialName) == aiReturn_SUCCESS) {
                OutMaterial.Name = MaterialName.C_Str();
            }

            aiColor4D DiffuseColor{};
            if (aiGetMaterialColor(&MaterialData, AI_MATKEY_COLOR_DIFFUSE, &DiffuseColor) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::DiffuseColor, MaterialMap{ ToVec4(DiffuseColor) });
            }

            aiColor4D BaseColor{};
            if (aiGetMaterialColor(&MaterialData, AI_MATKEY_BASE_COLOR, &BaseColor) == aiReturn_SUCCESS) {
                OutMaterial.PBR = true;
                AppendProperty(OutMaterial, MaterialType::BaseColor, MaterialMap{ ToVec4(BaseColor) });
            }

            float Value{ 0.0f };
            if (aiGetMaterialFloat(&MaterialData, AI_MATKEY_ROUGHNESS_FACTOR, &Value) == aiReturn_SUCCESS) {
                OutMaterial.PBR = true;
                AppendProperty(OutMaterial, MaterialType::Roughness, MaterialMap{ Value });
            }

            if (aiGetMaterialFloat(&MaterialData, AI_MATKEY_METALLIC_FACTOR, &Value) == aiReturn_SUCCESS) {
                OutMaterial.PBR = true;
                AppendProperty(OutMaterial, MaterialType::Metalness, MaterialMap{ Value });
            }

            if (aiGetMaterialFloat(&MaterialData, AI_MATKEY_OPACITY, &Value) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::Opacity, MaterialMap{ Value });
            }

            aiColor4D Emissive{};
            if (aiGetMaterialColor(&MaterialData, AI_MATKEY_COLOR_EMISSIVE, &Emissive) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::EmissionColorPbr, MaterialMap{ Vec3{ Emissive.r, Emissive.g, Emissive.b } });
            }

            AppendTextureProperty(MaterialData, aiTextureType_DIFFUSE, MaterialType::DiffuseColorMap, OutMaterial);
            AppendTextureProperty(MaterialData, aiTextureType_BASE_COLOR, MaterialType::BaseColorMap, OutMaterial);
            AppendTextureProperty(MaterialData, aiTextureType_NORMALS, MaterialType::NormalMapPbrMap, OutMaterial);
            AppendTextureProperty(MaterialData, aiTextureType_METALNESS, MaterialType::MetalnessMap, OutMaterial);
            AppendTextureProperty(MaterialData, aiTextureType_DIFFUSE_ROUGHNESS, MaterialType::RoughnessMap, OutMaterial);
            AppendTextureProperty(MaterialData, aiTextureType_EMISSIVE, MaterialType::EmissionColorPbrMap, OutMaterial);
            AppendTextureProperty(MaterialData, aiTextureType_OPACITY, MaterialType::OpacityMap, OutMaterial);
            AppendTextureProperty(MaterialData, aiTextureType_AMBIENT_OCCLUSION, MaterialType::AmbientOcclusionMap, OutMaterial);
        }

        struct BoneInfluence final {
        public:
            UVec4 Indices{ 0U, 0U, 0U, 0U };
            Vec4 Weights{ 0.0f, 0.0f, 0.0f, 0.0f };
        };

        float GetWeightComponent(const Vec4& Value, unsigned int Index) {
            if (Index == 0) {
                return Value.x;
            }

            if (Index == 1) {
                return Value.y;
            }

            if (Index == 2) {
                return Value.z;
            }

            return Value.w;
        }

        void SetWeightComponent(Vec4& Value, unsigned int Index, float Weight) {
            if (Index == 0) {
                Value.x = Weight;
                return;
            }

            if (Index == 1) {
                Value.y = Weight;
                return;
            }

            if (Index == 2) {
                Value.z = Weight;
                return;
            }

            Value.w = Weight;
        }

        void InsertBoneInfluence(BoneInfluence& Influence, std::uint32_t JointIndex, float Weight) {
            for (unsigned int Index{ 0 }; Index < MaxBoneInfluenceCount; ++Index) {
                if (GetWeightComponent(Influence.Weights, Index) == 0.0f) {
                    Influence.Indices[Index] = JointIndex;
                    SetWeightComponent(Influence.Weights, Index, Weight);
                    return;
                }
            }

            unsigned int MinIndex{ 0 };
            for (unsigned int Index{ 1 }; Index < MaxBoneInfluenceCount; ++Index) {
                if (GetWeightComponent(Influence.Weights, Index) < GetWeightComponent(Influence.Weights, MinIndex)) {
                    MinIndex = Index;
                }
            }

            if (Weight > GetWeightComponent(Influence.Weights, MinIndex)) {
                Influence.Indices[MinIndex] = JointIndex;
                SetWeightComponent(Influence.Weights, MinIndex, Weight);
            }
        }

        void NormalizeBoneInfluence(BoneInfluence& Influence) {
            const float Sum{ Influence.Weights.x + Influence.Weights.y + Influence.Weights.z + Influence.Weights.w };
            if (Sum > std::numeric_limits<float>::epsilon()) {
                Influence.Weights /= Sum;
            }
        }

        std::uint32_t ResolveNodeGlobalJointIndex(const aiBone& Bone, std::unordered_map<std::string, std::uint32_t>& InOutJointLookup, std::vector<ModelBoneInfo>& InOutBoneInfos) {
            const std::string BoneName{ Bone.mName.C_Str() };
            const std::unordered_map<std::string, std::uint32_t>::const_iterator FoundJointIndex{ InOutJointLookup.find(BoneName) };
            if (FoundJointIndex != InOutJointLookup.end()) {
                return FoundJointIndex->second;
            }

            const std::uint32_t JointIndex{ static_cast<std::uint32_t>(InOutBoneInfos.size()) };
            ModelBoneInfo BoneInfo{};
            BoneInfo.SkinArrayIndex = 0;
            BoneInfo.JointArrayIndex = JointIndex;
            BoneInfo.BoneName = BoneName;
            BoneInfo.InverseBindMatrix = ToMat4(Bone.mOffsetMatrix);
            InOutBoneInfos.push_back(std::move(BoneInfo));
            InOutJointLookup.insert_or_assign(BoneName, JointIndex);
            return JointIndex;
        }

        void ProcessMesh(const aiMesh& Mesh, std::uint32_t MaterialIndex, bool IsUvFlipEnabled, ModelNode& OutNode, std::unordered_map<std::string, std::uint32_t>& InOutJointLookup) {
            VertexAttributes& Vertices{ OutNode.Vertices() };
            std::vector<std::uint32_t>& Indices{ OutNode.Indices() };
            std::vector<ModelBoneInfo>& BoneInfos{ OutNode.BoneInfos() };
            const std::size_t BaseVertex{ Vertices.VertexCount() };
            const std::size_t BaseIndexOffset{ Indices.size() };
            const bool IsSkinnedMesh{ Mesh.HasBones() };

            if (BaseVertex == 0) {
                OutNode.SetIsSkinnedMesh(IsSkinnedMesh);
            }
            else if (OutNode.IsSkinnedMesh() != IsSkinnedMesh) {
                throw AssetError{ "Node contains mixed static and skinned meshes." };
            }

            Vertices.Reserve(BaseVertex + Mesh.mNumVertices);

            std::vector<BoneInfluence> BoneInfluences{};
            BoneInfluences.resize(Mesh.mNumVertices);

            for (unsigned int BoneIndex{ 0 }; BoneIndex < Mesh.mNumBones; ++BoneIndex) {
                const aiBone& Bone{ *Mesh.mBones[BoneIndex] };
                const std::uint32_t JointIndex{ ResolveNodeGlobalJointIndex(Bone, InOutJointLookup, BoneInfos) };

                for (unsigned int WeightIndex{ 0 }; WeightIndex < Bone.mNumWeights; ++WeightIndex) {
                    const aiVertexWeight& Weight{ Bone.mWeights[WeightIndex] };
                    if (Weight.mVertexId < BoneInfluences.size()) {
                        InsertBoneInfluence(BoneInfluences[Weight.mVertexId], JointIndex, Weight.mWeight);
                    }
                }
            }

            for (unsigned int VertexIndex{ 0 }; VertexIndex < Mesh.mNumVertices; ++VertexIndex) {
                Vertices.Positions.push_back(ToVec3(Mesh.mVertices[VertexIndex]));
                Vertices.Normals.push_back(Mesh.HasNormals() ? ToVec3(Mesh.mNormals[VertexIndex]) : Vec3{ 0.0f, 1.0f, 0.0f });
                Vertices.Colors.push_back(Mesh.HasVertexColors(0) ? ToVec4(Mesh.mColors[0][VertexIndex]) : Vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
                Vertices.Tangents.push_back(Mesh.HasTangentsAndBitangents() ? ToVec3(Mesh.mTangents[VertexIndex]) : Vec3{});
                Vertices.Bitangents.push_back(Mesh.HasTangentsAndBitangents() ? ToVec3(Mesh.mBitangents[VertexIndex]) : Vec3{});

                for (unsigned int ChannelIndex{ 0 }; ChannelIndex < 4; ++ChannelIndex) {
                    Vertices.TexCoords[ChannelIndex].push_back(Mesh.HasTextureCoords(ChannelIndex) ? ToVec2(Mesh.mTextureCoords[ChannelIndex][VertexIndex], IsUvFlipEnabled) : Vec2{});
                }

                NormalizeBoneInfluence(BoneInfluences[VertexIndex]);
                Vertices.BoneIndices.push_back(BoneInfluences[VertexIndex].Indices);
                Vertices.BoneWeights.push_back(BoneInfluences[VertexIndex].Weights);
            }

            for (unsigned int FaceIndex{ 0 }; FaceIndex < Mesh.mNumFaces; ++FaceIndex) {
                const aiFace& Face{ Mesh.mFaces[FaceIndex] };
                for (unsigned int Index{ 0 }; Index < Face.mNumIndices; ++Index) {
                    Indices.push_back(static_cast<std::uint32_t>(BaseVertex + Face.mIndices[Index]));
                }
            }

            if (Indices.size() > BaseIndexOffset) {
                ModelNode::SubMesh SubMesh{};
                SubMesh.IndexOffset = BaseIndexOffset;
                SubMesh.IndexCount = Indices.size() - BaseIndexOffset;
                SubMesh.MaterialGroupItemIndex = MaterialIndex;
                OutNode.SubMeshes().push_back(SubMesh);
            }
        }

        void BuildNodeRecursive(const aiScene& Scene, const aiNode& SceneNode, ModelResult& OutModelData, ModelNode* ParentNode, bool IsUvFlipEnabled) {
            ModelNode& Node{ OutModelData.CreateNode(SceneNode.mName.C_Str(), ParentNode) };
            Node.SetNodeToParent(ToMat4(SceneNode.mTransformation));

            std::unordered_map<std::string, std::uint32_t> JointLookup{};
            for (unsigned int MeshIndex{ 0 }; MeshIndex < SceneNode.mNumMeshes; ++MeshIndex) {
                const aiMesh& Mesh{ *Scene.mMeshes[SceneNode.mMeshes[MeshIndex]] };
                ProcessMesh(Mesh, Mesh.mMaterialIndex, IsUvFlipEnabled, Node, JointLookup);
            }

            for (unsigned int ChildIndex{ 0 }; ChildIndex < SceneNode.mNumChildren; ++ChildIndex) {
                BuildNodeRecursive(Scene, *SceneNode.mChildren[ChildIndex], OutModelData, &Node, IsUvFlipEnabled);
            }
        }
    }

    AssimpAssetImporter::AssimpAssetImporter(GraphicsAPI Api)
        : mApi{ Api } {
    }

    void AssimpAssetImporter::LoadFromFile(std::string_view FilePath, ModelResult& OutModelData, std::vector<MaterialGroup>& OutMaterialGroups, bool IsUvFlipEnabled) {
        Assimp::Importer Importer{};
        unsigned int Flags{ aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace | aiProcess_ImproveCacheLocality | aiProcess_LimitBoneWeights | aiProcess_ValidateDataStructure };
        if (mApi == GraphicsAPI::DirectX) {
            Flags |= aiProcess_ConvertToLeftHanded;
        }

        const aiScene* Scene{ Importer.ReadFile(std::string{ FilePath }, Flags) };
        if (Scene == nullptr || Scene->mRootNode == nullptr) {
            throw AssetError{ Importer.GetErrorString() };
        }

        OutModelData = ModelResult{};
        OutMaterialGroups.clear();

        MaterialGroup Group{};
        Group.Name = std::string{ FilePath };
        Group.Items.reserve(Scene->mNumMaterials);

        for (unsigned int MaterialIndex{ 0 }; MaterialIndex < Scene->mNumMaterials; ++MaterialIndex) {
            MaterialGroupItem Item{};
            FillMaterial(*Scene->mMaterials[MaterialIndex], Item.MaterialData);
            Group.Items.push_back(std::move(Item));
        }

        if (Group.Items.empty()) {
            Group.Items.push_back(MaterialGroupItem{});
        }

        OutMaterialGroups.push_back(std::move(Group));
        BuildNodeRecursive(*Scene, *Scene->mRootNode, OutModelData, nullptr, IsUvFlipEnabled);
    }
}
