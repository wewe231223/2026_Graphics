#include "AssimpAssetImporter.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <initializer_list>
#include <limits>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
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

        bool TryParseEmbeddedTextureIndex(const std::string& TexturePath, unsigned int& OutTextureIndex) {
            if (TexturePath.size() <= 1 || TexturePath[0] != '*') {
                return false;
            }

            for (std::size_t CharacterIndex{ 1 }; CharacterIndex < TexturePath.size(); ++CharacterIndex) {
                if (std::isdigit(static_cast<unsigned char>(TexturePath[CharacterIndex])) == 0) {
                    return false;
                }
            }

            OutTextureIndex = static_cast<unsigned int>(std::stoul(TexturePath.substr(1)));
            return true;
        }

        std::string ResolveEmbeddedTexturePath(const aiScene& SceneData, const std::string& TexturePath) {
            unsigned int TextureIndex{ 0 };
            if (TryParseEmbeddedTextureIndex(TexturePath, TextureIndex) == false) {
                return TexturePath;
            }

            if (TextureIndex >= SceneData.mNumTextures || SceneData.mTextures == nullptr || SceneData.mTextures[TextureIndex] == nullptr) {
                return std::string{ "EmbeddedTexture_" } + std::to_string(TextureIndex);
            }

            const aiTexture& EmbeddedTexture{ *SceneData.mTextures[TextureIndex] };
            if (EmbeddedTexture.mFilename.length > 0) {
                return std::string{ EmbeddedTexture.mFilename.C_Str() };
            }

            std::string FormatHint{};
            if (EmbeddedTexture.achFormatHint[0] != '\0') {
                FormatHint = std::string{ EmbeddedTexture.achFormatHint };
            }

            if (FormatHint.empty()) {
                FormatHint = std::string{ "bin" };
            }

            return std::string{ "EmbeddedTexture_" } + std::to_string(TextureIndex) + std::string{ "." } + FormatHint;
        }

        void AppendTextureProperty(const aiScene& SceneData, const aiMaterial& MaterialData, aiTextureType TextureType, MaterialType Type, Material& OutMaterial) {
            aiString TexturePath{};
            if (MaterialData.GetTexture(TextureType, 0, &TexturePath) == aiReturn_SUCCESS) {
                const std::string ResolvedTexturePath{ ResolveEmbeddedTexturePath(SceneData, std::string{ TexturePath.C_Str() }) };
                AppendProperty(OutMaterial, Type, MaterialMap{ ResolvedTexturePath });
            }
        }

        void AppendTexturePropertyByTypes(const aiScene& SceneData, const aiMaterial& MaterialData, MaterialType Type, std::initializer_list<aiTextureType> TextureTypes, Material& OutMaterial) {
            for (const aiTextureType TextureType : TextureTypes) {
                aiString TexturePath{};
                if (MaterialData.GetTexture(TextureType, 0, &TexturePath) == aiReturn_SUCCESS) {
                    const std::string ResolvedTexturePath{ ResolveEmbeddedTexturePath(SceneData, std::string{ TexturePath.C_Str() }) };
                    AppendProperty(OutMaterial, Type, MaterialMap{ ResolvedTexturePath });
                    return;
                }
            }
        }

        void FillMaterial(const aiScene& SceneData, const aiMaterial& MaterialData, Material& OutMaterial) {
            aiString MaterialName{};
            if (MaterialData.Get(AI_MATKEY_NAME, MaterialName) == aiReturn_SUCCESS) {
                OutMaterial.Name = MaterialName.C_Str();
            }

            int IntegerValue{ 0 };
            if (aiGetMaterialInteger(&MaterialData, AI_MATKEY_SHADING_MODEL, &IntegerValue) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::ShadingModel, MaterialMap{ static_cast<std::int64_t>(IntegerValue) });
            }

            if (aiGetMaterialInteger(&MaterialData, AI_MATKEY_TWOSIDED, &IntegerValue) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::TwoSided, MaterialMap{ IntegerValue != 0 });
            }

            if (aiGetMaterialInteger(&MaterialData, AI_MATKEY_ENABLE_WIREFRAME, &IntegerValue) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::Wireframe, MaterialMap{ IntegerValue != 0 });
            }

            if (aiGetMaterialInteger(&MaterialData, AI_MATKEY_BLEND_FUNC, &IntegerValue) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::BlendMode, MaterialMap{ static_cast<std::int64_t>(IntegerValue) });
            }

            float FloatValue{ 0.0f };
            if (aiGetMaterialFloat(&MaterialData, AI_MATKEY_OPACITY, &FloatValue) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::Opacity, MaterialMap{ FloatValue });
            }

            aiString AlphaMode{};
#ifdef AI_MATKEY_GLTF_ALPHAMODE
            if (MaterialData.Get(AI_MATKEY_GLTF_ALPHAMODE, AlphaMode) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::AlphaMode, MaterialMap{ std::string{ AlphaMode.C_Str() } });
            }
#endif

#ifdef AI_MATKEY_GLTF_ALPHACUTOFF
            if (aiGetMaterialFloat(&MaterialData, AI_MATKEY_GLTF_ALPHACUTOFF, &FloatValue) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::AlphaCutoff, MaterialMap{ FloatValue });
            }
#endif

            aiColor4D ColorValue{};
            if (aiGetMaterialColor(&MaterialData, AI_MATKEY_BASE_COLOR, &ColorValue) == aiReturn_SUCCESS) {
                OutMaterial.PBR = true;
                AppendProperty(OutMaterial, MaterialType::BaseColor, MaterialMap{ Vec4{ ColorValue.r, ColorValue.g, ColorValue.b, ColorValue.a } });
            }

            if (aiGetMaterialColor(&MaterialData, AI_MATKEY_COLOR_DIFFUSE, &ColorValue) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::DiffuseColor, MaterialMap{ Vec4{ ColorValue.r, ColorValue.g, ColorValue.b, ColorValue.a } });
            }

            if (aiGetMaterialColor(&MaterialData, AI_MATKEY_COLOR_AMBIENT, &ColorValue) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::AmbientColor, MaterialMap{ Vec4{ ColorValue.r, ColorValue.g, ColorValue.b, ColorValue.a } });
            }

            if (aiGetMaterialColor(&MaterialData, AI_MATKEY_COLOR_SPECULAR, &ColorValue) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::SpecularColor, MaterialMap{ Vec4{ ColorValue.r, ColorValue.g, ColorValue.b, ColorValue.a } });
            }

            if (aiGetMaterialColor(&MaterialData, AI_MATKEY_COLOR_EMISSIVE, &ColorValue) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::EmissiveColor, MaterialMap{ Vec4{ ColorValue.r, ColorValue.g, ColorValue.b, ColorValue.a } });
            }

            if (aiGetMaterialColor(&MaterialData, AI_MATKEY_COLOR_TRANSPARENT, &ColorValue) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::TransparentColor, MaterialMap{ Vec4{ ColorValue.r, ColorValue.g, ColorValue.b, ColorValue.a } });
            }

            if (aiGetMaterialColor(&MaterialData, AI_MATKEY_COLOR_REFLECTIVE, &ColorValue) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::ReflectiveColor, MaterialMap{ Vec4{ ColorValue.r, ColorValue.g, ColorValue.b, ColorValue.a } });
            }

            if (aiGetMaterialFloat(&MaterialData, AI_MATKEY_METALLIC_FACTOR, &FloatValue) == aiReturn_SUCCESS) {
                OutMaterial.PBR = true;
                AppendProperty(OutMaterial, MaterialType::MetallicFactor, MaterialMap{ FloatValue });
            }

            if (aiGetMaterialFloat(&MaterialData, AI_MATKEY_ROUGHNESS_FACTOR, &FloatValue) == aiReturn_SUCCESS) {
                OutMaterial.PBR = true;
                AppendProperty(OutMaterial, MaterialType::RoughnessFactor, MaterialMap{ FloatValue });
            }

            if (aiGetMaterialFloat(&MaterialData, AI_MATKEY_BUMPSCALING, &FloatValue) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::NormalScale, MaterialMap{ FloatValue });
            }

#ifdef AI_MATKEY_GLTF_TEXTURE_STRENGTH
            if (aiGetMaterialFloat(&MaterialData, AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_AMBIENT_OCCLUSION, 0), &FloatValue) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::OcclusionStrength, MaterialMap{ FloatValue });
            }
#endif

            if (aiGetMaterialFloat(&MaterialData, AI_MATKEY_EMISSIVE_INTENSITY, &FloatValue) == aiReturn_SUCCESS) {
                AppendProperty(OutMaterial, MaterialType::EmissiveStrength, MaterialMap{ FloatValue });
            }

            AppendTexturePropertyByTypes(SceneData, MaterialData, MaterialType::DiffuseTexture, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }, OutMaterial);
            AppendTextureProperty(SceneData, MaterialData, aiTextureType_SPECULAR, MaterialType::SpecularTexture, OutMaterial);
            AppendTextureProperty(SceneData, MaterialData, aiTextureType_AMBIENT, MaterialType::AmbientTexture, OutMaterial);
            AppendTextureProperty(SceneData, MaterialData, aiTextureType_EMISSIVE, MaterialType::EmissiveTexture, OutMaterial);
            AppendTextureProperty(SceneData, MaterialData, aiTextureType_OPACITY, MaterialType::OpacityTexture, OutMaterial);
            AppendTextureProperty(SceneData, MaterialData, aiTextureType_SHININESS, MaterialType::ShininessTexture, OutMaterial);
            AppendTexturePropertyByTypes(SceneData, MaterialData, MaterialType::HeightBumpTexture, { aiTextureType_HEIGHT, aiTextureType_NORMAL_CAMERA }, OutMaterial);
            AppendTextureProperty(SceneData, MaterialData, aiTextureType_NORMALS, MaterialType::NormalTexture, OutMaterial);
            AppendTextureProperty(SceneData, MaterialData, aiTextureType_DISPLACEMENT, MaterialType::DisplacementTexture, OutMaterial);
            AppendTextureProperty(SceneData, MaterialData, aiTextureType_REFLECTION, MaterialType::ReflectionTexture, OutMaterial);
            AppendTexturePropertyByTypes(SceneData, MaterialData, MaterialType::LightmapTexture, { aiTextureType_LIGHTMAP, aiTextureType_AMBIENT_OCCLUSION }, OutMaterial);
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

        std::uint32_t ResolveNodeGlobalJointIndex(const aiBone& Bone, std::uint32_t SkinArrayIndex, std::unordered_map<std::string, std::uint32_t>& InOutJointLookup, std::vector<ModelBoneInfo>& InOutBoneInfos) {
            const std::string BoneName{ Bone.mName.C_Str() };
            const std::unordered_map<std::string, std::uint32_t>::const_iterator FoundJointIndex{ InOutJointLookup.find(BoneName) };
            if (FoundJointIndex != InOutJointLookup.end()) {
                return FoundJointIndex->second;
            }

            const std::uint32_t JointIndex{ static_cast<std::uint32_t>(InOutBoneInfos.size()) };
            ModelBoneInfo BoneInfo{};
            BoneInfo.SkinArrayIndex = SkinArrayIndex;
            BoneInfo.JointArrayIndex = JointIndex;
            BoneInfo.BoneName = BoneName;
            BoneInfo.InverseBindMatrix = ToMat4(Bone.mOffsetMatrix);
            InOutBoneInfos.push_back(std::move(BoneInfo));
            InOutJointLookup.insert_or_assign(BoneName, JointIndex);
            return JointIndex;
        }

        void ProcessMesh(const aiMesh& Mesh, std::uint32_t MaterialIndex, bool IsUvFlipEnabled, std::uint32_t SkinArrayIndex, ModelNode& OutNode, std::unordered_map<std::string, std::uint32_t>& InOutJointLookup) {
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
                const std::uint32_t JointIndex{ ResolveNodeGlobalJointIndex(Bone, SkinArrayIndex, InOutJointLookup, BoneInfos) };

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

        void BuildAssimpNodeLookupRecursive(const aiNode& SceneNode, std::unordered_map<std::string, const aiNode*>& InOutNodeLookup) {
            InOutNodeLookup.insert_or_assign(std::string{ SceneNode.mName.C_Str() }, &SceneNode);

            for (unsigned int ChildIndex{ 0 }; ChildIndex < SceneNode.mNumChildren; ++ChildIndex) {
                BuildAssimpNodeLookupRecursive(*SceneNode.mChildren[ChildIndex], InOutNodeLookup);
            }
        }

        bool HasAncestorInSet(const aiNode* Node, const std::unordered_set<const aiNode*>& CandidateSet) {
            const aiNode* CurrentNode{ Node->mParent };
            while (CurrentNode != nullptr) {
                if (CandidateSet.find(CurrentNode) != CandidateSet.end()) {
                    return true;
                }

                CurrentNode = CurrentNode->mParent;
            }

            return false;
        }

        bool IsAncestorOrSame(const aiNode* AncestorNode, const aiNode* DescendantNode) {
            const aiNode* CurrentNode{ DescendantNode };
            while (CurrentNode != nullptr) {
                if (CurrentNode == AncestorNode) {
                    return true;
                }

                CurrentNode = CurrentNode->mParent;
            }

            return false;
        }

        void CollectSceneBoneNodes(const aiScene& Scene, const std::unordered_map<std::string, const aiNode*>& NodeLookup, std::unordered_set<const aiNode*>& OutBoneNodeSet) {
            for (unsigned int MeshIndex{ 0 }; MeshIndex < Scene.mNumMeshes; ++MeshIndex) {
                const aiMesh& Mesh{ *Scene.mMeshes[MeshIndex] };
                for (unsigned int BoneIndex{ 0 }; BoneIndex < Mesh.mNumBones; ++BoneIndex) {
                    const aiBone& Bone{ *Mesh.mBones[BoneIndex] };
                    const std::unordered_map<std::string, const aiNode*>::const_iterator FoundBoneNode{ NodeLookup.find(std::string{ Bone.mName.C_Str() }) };
                    if (FoundBoneNode == NodeLookup.end()) {
                        continue;
                    }

                    OutBoneNodeSet.insert(FoundBoneNode->second);
                }
            }
        }

        const aiNode* ResolveTopBoneTreeRootNode(const aiNode* BoneNode, const std::unordered_set<const aiNode*>& SceneBoneNodeSet) {
            const aiNode* TopBoneTreeRootNode{ BoneNode };
            while (TopBoneTreeRootNode->mParent != nullptr && SceneBoneNodeSet.find(TopBoneTreeRootNode->mParent) != SceneBoneNodeSet.end()) {
                TopBoneTreeRootNode = TopBoneTreeRootNode->mParent;
            }

            return TopBoneTreeRootNode;
        }

        const aiNode* ResolveSkinBoneRootNode(const aiScene& Scene, const aiNode& SceneNode, const std::unordered_map<std::string, const aiNode*>& NodeLookup) {
            std::unordered_set<const aiNode*> BoneNodeSet{};
            std::unordered_set<const aiNode*> SceneBoneNodeSet{};
            CollectSceneBoneNodes(Scene, NodeLookup, SceneBoneNodeSet);

            for (unsigned int MeshIndex{ 0 }; MeshIndex < SceneNode.mNumMeshes; ++MeshIndex) {
                const aiMesh& Mesh{ *Scene.mMeshes[SceneNode.mMeshes[MeshIndex]] };
                for (unsigned int BoneIndex{ 0 }; BoneIndex < Mesh.mNumBones; ++BoneIndex) {
                    const aiBone& Bone{ *Mesh.mBones[BoneIndex] };
                    const std::unordered_map<std::string, const aiNode*>::const_iterator FoundBoneNode{ NodeLookup.find(std::string{ Bone.mName.C_Str() }) };
                    if (FoundBoneNode == NodeLookup.end()) {
                        continue;
                    }

                    BoneNodeSet.insert(FoundBoneNode->second);
                }
            }

            if (BoneNodeSet.empty() == true) {
                return nullptr;
            }

            std::vector<const aiNode*> RootBoneCandidates{};
            RootBoneCandidates.reserve(BoneNodeSet.size());
            for (const aiNode* BoneNode : BoneNodeSet) {
                if (HasAncestorInSet(BoneNode, BoneNodeSet) == false) {
                    RootBoneCandidates.push_back(BoneNode);
                }
            }

            if (RootBoneCandidates.size() == 1) {
                return ResolveTopBoneTreeRootNode(RootBoneCandidates[0], SceneBoneNodeSet);
            }

            const aiNode* BestRootBoneCandidate{ nullptr };
            std::size_t BestInfluencedBoneCount{ 0 };
            for (const aiNode* CandidateRootBone : RootBoneCandidates) {
                std::size_t InfluencedBoneCount{ 0 };
                for (const aiNode* BoneNode : BoneNodeSet) {
                    if (IsAncestorOrSame(CandidateRootBone, BoneNode) == true) {
                        ++InfluencedBoneCount;
                    }
                }

                if (BestRootBoneCandidate == nullptr || InfluencedBoneCount > BestInfluencedBoneCount) {
                    BestRootBoneCandidate = CandidateRootBone;
                    BestInfluencedBoneCount = InfluencedBoneCount;
                    continue;
                }

                if (InfluencedBoneCount == BestInfluencedBoneCount) {
                    const std::string CurrentBestCandidateName{ BestRootBoneCandidate->mName.C_Str() };
                    const std::string NewCandidateName{ CandidateRootBone->mName.C_Str() };
                    if (NewCandidateName < CurrentBestCandidateName) {
                        BestRootBoneCandidate = CandidateRootBone;
                        BestInfluencedBoneCount = InfluencedBoneCount;
                    }
                }
            }

            if (BestRootBoneCandidate == nullptr) {
                return nullptr;
            }

            return ResolveTopBoneTreeRootNode(BestRootBoneCandidate, SceneBoneNodeSet);
        }

        void BuildNodeRecursive(const aiScene& Scene, const aiNode& SceneNode, const std::unordered_map<std::string, const aiNode*>& NodeLookup, ModelResult& OutModelData, ModelNode* ParentNode, bool IsUvFlipEnabled) {
            ModelNode& Node{ OutModelData.CreateNode(SceneNode.mName.C_Str(), ParentNode) };
            Node.SetNodeToParent(ToMat4(SceneNode.mTransformation));
            const std::uint32_t SkinArrayIndex{ Node.GetId() };

            std::unordered_map<std::string, std::uint32_t> JointLookup{};
            for (unsigned int MeshIndex{ 0 }; MeshIndex < SceneNode.mNumMeshes; ++MeshIndex) {
                const aiMesh& Mesh{ *Scene.mMeshes[SceneNode.mMeshes[MeshIndex]] };
                ProcessMesh(Mesh, Mesh.mMaterialIndex, IsUvFlipEnabled, SkinArrayIndex, Node, JointLookup);
            }

            if (Node.IsSkinnedMesh() == true) {
                const aiNode* SkinBoneRootNode{ ResolveSkinBoneRootNode(Scene, SceneNode, NodeLookup) };
                if (SkinBoneRootNode != nullptr) {
                    Node.SetSkinBoneRootNodeName(std::string{ SkinBoneRootNode->mName.C_Str() });
                }
            }

            for (unsigned int ChildIndex{ 0 }; ChildIndex < SceneNode.mNumChildren; ++ChildIndex) {
                BuildNodeRecursive(Scene, *SceneNode.mChildren[ChildIndex], NodeLookup, OutModelData, &Node, IsUvFlipEnabled);
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
            FillMaterial(*Scene, *Scene->mMaterials[MaterialIndex], Item.MaterialData);
            Group.Items.push_back(std::move(Item));
        }

        if (Group.Items.empty()) {
            Group.Items.push_back(MaterialGroupItem{});
        }

        OutMaterialGroups.push_back(std::move(Group));

        std::unordered_map<std::string, const aiNode*> NodeLookup{};
        BuildAssimpNodeLookupRecursive(*Scene->mRootNode, NodeLookup);
        BuildNodeRecursive(*Scene, *Scene->mRootNode, NodeLookup, OutModelData, nullptr, IsUvFlipEnabled);
    }
}
