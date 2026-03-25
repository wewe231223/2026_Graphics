#include "SkinnedMeshRenderSystem.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Game/Base/Common.h"
#include "Game/Model/AssetRegistry.h"
#include "Game/Model/Model.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/SkinnedMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    constexpr std::uint32_t SkinnedModelContextFlagBitMask{ 0x1u };
    constexpr std::uint32_t PickedDrawFlagBitMask{ 0x1u };

    SimpleMath::Matrix BuildLocalWorldMatrix(const Game::Transform& TransformComponent) {
        const SimpleMath::Matrix TrsMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
        return TransformComponent.nodeToParent * TrsMatrix;
    }

    bool IsEntityWithinPickedHierarchy(Arche::World& World, Arche::EntityID EntityId, Arche::EntityID PickedEntityId) {
        if (PickedEntityId == Arche::NullEntityID) {
            return false;
        }

        Arche::EntityID CurrentEntityId{ EntityId };
        while (CurrentEntityId != Arche::NullEntityID) {
            if (CurrentEntityId == PickedEntityId) {
                return true;
            }

            const Game::EntityHierarchy* Hierarchy{ World.GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (Hierarchy == nullptr) {
                break;
            }

            CurrentEntityId = Hierarchy->parent;
        }

        return false;
    }

    bool TryResolveWorldMatrix(Arche::World& World, Arche::EntityID EntityId, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, SimpleMath::Matrix& OutWorldMatrix) {
        const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CachedWorldMatrixIter{ InOutWorldMatrices.find(EntityId) };
        if (CachedWorldMatrixIter != InOutWorldMatrices.end()) {
            OutWorldMatrix = CachedWorldMatrixIter->second;
            return true;
        }

        std::vector<Arche::EntityID> EntityPath{};
        Arche::EntityID CurrentEntityId{ EntityId };
        SimpleMath::Matrix ParentWorldMatrix{ SimpleMath::Matrix::Identity };

        while (CurrentEntityId != Arche::NullEntityID) {
            const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CurrentCachedWorldMatrixIter{ InOutWorldMatrices.find(CurrentEntityId) };
            if (CurrentCachedWorldMatrixIter != InOutWorldMatrices.end()) {
                ParentWorldMatrix = CurrentCachedWorldMatrixIter->second;
                break;
            }

            const Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(CurrentEntityId) };
            const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (TransformComponent == nullptr || HierarchyComponent == nullptr) {
                return false;
            }

            EntityPath.push_back(CurrentEntityId);
            CurrentEntityId = HierarchyComponent->parent;
        }

        for (std::vector<Arche::EntityID>::const_reverse_iterator EntityPathIter{ EntityPath.crbegin() }; EntityPathIter != EntityPath.crend(); ++EntityPathIter) {
            const Arche::EntityID CurrentPathEntityId{ *EntityPathIter };
            const Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(CurrentPathEntityId) };
            if (TransformComponent == nullptr) {
                return false;
            }

            const SimpleMath::Matrix LocalWorldMatrix{ BuildLocalWorldMatrix(*TransformComponent) };
            const SimpleMath::Matrix CurrentWorldMatrix{ LocalWorldMatrix * ParentWorldMatrix };
            InOutWorldMatrices[CurrentPathEntityId] = CurrentWorldMatrix;
            ParentWorldMatrix = CurrentWorldMatrix;
        }

        OutWorldMatrix = ParentWorldMatrix;
        return true;
    }

    void GatherBoneMatricesRecursive(Arche::World& World, Arche::EntityID EntityId, Game::Model* ModelData, std::uint32_t SkinArrayIndex, const SimpleMath::Matrix& MeshWorldInverseMatrix, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, std::vector<SimpleMath::Matrix>& InOutBoneMatrices) {
        if (EntityId == Arche::NullEntityID || ModelData == nullptr) {
            return;
        }

        const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(EntityId) };
        if (HierarchyComponent == nullptr) {
            return;
        }

        const Game::Bone* BoneComponent{ World.GetComponent<Game::Bone>(EntityId) };
        if (BoneComponent != nullptr && BoneComponent->model == ModelData) {
            SimpleMath::Matrix BoneWorldMatrix{};
            const bool IsBoneWorldMatrixResolved{ TryResolveWorldMatrix(World, EntityId, InOutWorldMatrices, BoneWorldMatrix) };
            if (IsBoneWorldMatrixResolved == true) {
                const std::span<const Game::RuntimeBoneInfo> RuntimeBoneInfos{ ModelData->GetRuntimeBoneInfos(BoneComponent->runtimeBoneInfoOffset, BoneComponent->runtimeBoneInfoCount) };
                for (const Game::RuntimeBoneInfo& RuntimeBoneInfoItem : RuntimeBoneInfos) {
                    if (RuntimeBoneInfoItem.SkinArrayIndex != SkinArrayIndex) {
                        continue;
                    }

                    if (RuntimeBoneInfoItem.JointArrayIndex >= InOutBoneMatrices.size()) {
                        continue;
                    }

                    InOutBoneMatrices[RuntimeBoneInfoItem.JointArrayIndex] = RuntimeBoneInfoItem.InverseBindMatrix * BoneWorldMatrix * MeshWorldInverseMatrix;
                }
            }
        }

        Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
        while (ChildEntityId != Arche::NullEntityID) {
            const Game::EntityHierarchy* ChildHierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(ChildEntityId) };
            if (ChildHierarchyComponent == nullptr) {
                break;
            }

            GatherBoneMatricesRecursive(World, ChildEntityId, ModelData, SkinArrayIndex, MeshWorldInverseMatrix, InOutWorldMatrices, InOutBoneMatrices);
            ChildEntityId = ChildHierarchyComponent->nextSibling;
        }
    }

    bool TryBuildBoneMatricesFallback(Arche::World& World, Game::BoneSkinReference& BoneSkinReferenceComponent, Game::SkinnedMeshRenderer& SkinnedMeshRendererComponent, const SimpleMath::Matrix& MeshWorldMatrix, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, std::uint32_t& OutSkinArrayIndex, std::vector<SimpleMath::Matrix>& OutBoneMatrices) {
        const std::vector<Game::ModelNode>& Nodes{ SkinnedMeshRendererComponent.model->GetNodes() };
        if (SkinnedMeshRendererComponent.nodeIndex >= Nodes.size()) {
            return false;
        }

        const Game::ModelNode& Node{ Nodes[SkinnedMeshRendererComponent.nodeIndex] };
        const std::uint32_t SkinArrayIndex{ Node.GetId() };
        std::uint32_t BoneMatrixCount{ 0 };
        const bool IsBoneMatrixCountResolved{ SkinnedMeshRendererComponent.model->TryGetRuntimeBoneMatrixCount(SkinArrayIndex, BoneMatrixCount) };
        if (IsBoneMatrixCountResolved == false || BoneMatrixCount == 0) {
            return false;
        }

        SimpleMath::Matrix MeshWorldInverseMatrix{ MeshWorldMatrix };
        MeshWorldInverseMatrix = MeshWorldInverseMatrix.Invert();

        std::vector<SimpleMath::Matrix> BoneMatrices{};
        BoneMatrices.resize(static_cast<std::size_t>(BoneMatrixCount), SimpleMath::Matrix::Identity);
        GatherBoneMatricesRecursive(World, BoneSkinReferenceComponent.boneRootEntityId, SkinnedMeshRendererComponent.model, SkinArrayIndex, MeshWorldInverseMatrix, InOutWorldMatrices, BoneMatrices);
        OutSkinArrayIndex = SkinArrayIndex;
        OutBoneMatrices = std::move(BoneMatrices);
        return true;
    }
}

namespace Game {
    const std::string& SkinnedMeshRenderSystem::Name() const {
        return mName;
    }

    Phase SkinnedMeshRenderSystem::GetPhase() const {
        return Phase::Render;
    }

    std::span<const ComponentAccess> SkinnedMeshRenderSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 4> Accesses{ { { typeid(BoneSkinReference), Access::Read }, { typeid(SkinnedMeshRenderer), Access::Read }, { typeid(Transform), Access::Read }, { typeid(EntityHierarchy), Access::Read } } };
        return Accesses;
    }

    std::span<const ResourceAccess> SkinnedMeshRenderSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 2> Accesses{ { { typeid(RFD::RenderFrameData), Access::Write }, { typeid(std::vector<RegisteredMaterialGroup>), Access::Read } } };
        return Accesses;
    }

    void SkinnedMeshRenderSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        RFD::RenderFrameData& RenderData{ Ctx.RenderData };
        const std::vector<RegisteredMaterialGroup>& MaterialGroups{ *Ctx.MaterialGroups };
        for (auto [BoneSkinReferenceComponent, SkinnedMeshRendererComponent, TransformComponent, EntityHierarchyComponent] : World.Query<BoneSkinReference, SkinnedMeshRenderer, Transform, EntityHierarchy>()) {
            (void)TransformComponent;
            (void)EntityHierarchyComponent;

            if (SkinnedMeshRendererComponent.active == false || SkinnedMeshRendererComponent.model == nullptr) {
                continue;
            }

            if (BoneSkinReferenceComponent.boneRootEntityId == Arche::NullEntityID) {
                continue;
            }

            const Arche::EntityID EntityId{ EntityHierarchyComponent.self };
            SimpleMath::Matrix MeshWorldMatrix{};
            const bool IsMeshWorldMatrixResolved{ TryResolveWorldMatrix(World, EntityId, Ctx.WorldMatrices, MeshWorldMatrix) };
            if (IsMeshWorldMatrixResolved == false) {
                continue;
            }

            const std::vector<ModelNode>& Nodes{ SkinnedMeshRendererComponent.model->GetNodes() };
            if (SkinnedMeshRendererComponent.nodeIndex >= Nodes.size()) {
                continue;
            }

            const ModelNode& Node{ Nodes[SkinnedMeshRendererComponent.nodeIndex] };

            std::uint32_t SkinArrayIndex{ 0 };
            std::vector<SimpleMath::Matrix> BoneMatrices{};
            bool IsCacheHit{ false };
            const std::unordered_map<Arche::EntityID, SkinnedPoseCacheEntry>::const_iterator CacheIter{ Ctx.SkinnedPoseCache.find(EntityId) };
            if (CacheIter != Ctx.SkinnedPoseCache.end()) {
                const SkinnedPoseCacheEntry& CacheEntry{ CacheIter->second };
                if (CacheEntry.IsValid == true && CacheEntry.SkinArrayIndex == Node.GetId() && CacheEntry.BoneMatrices.empty() == false) {
                    SkinArrayIndex = CacheEntry.SkinArrayIndex;
                    BoneMatrices = CacheEntry.BoneMatrices;
                    IsCacheHit = true;
                }
            }

            if (IsCacheHit == false) {
                const bool IsFallbackBuilt{ TryBuildBoneMatricesFallback(World, BoneSkinReferenceComponent, SkinnedMeshRendererComponent, MeshWorldMatrix, Ctx.WorldMatrices, SkinArrayIndex, BoneMatrices) };
                if (IsFallbackBuilt == false) {
                    continue;
                }
            }

            const std::vector<ModelSubMesh>& SubMeshes{ Node.GetSubMeshes() };
            if (SubMeshes.empty()) {
                continue;
            }

            const std::uint32_t BoneIndexStart{ static_cast<std::uint32_t>(RenderData.bonePalette.size()) };
            RenderData.bonePalette.insert(RenderData.bonePalette.end(), BoneMatrices.begin(), BoneMatrices.end());

            RFD::ModelContext ModelContext{};
            ModelContext.world = MeshWorldMatrix;
            ModelContext.prevWorld = ModelContext.world;
            ModelContext.flags = SkinnedModelContextFlagBitMask;
            ModelContext.boneIndexStart = BoneIndexStart;
            ModelContext.objectID = static_cast<std::uint32_t>(RenderData.modelContexts.size());
            RenderData.modelContexts.push_back(ModelContext);

            const Material* MaterialComponent{ World.GetComponent<Material>(EntityId) };

            for (std::size_t SubMeshIndex{ 0 }; SubMeshIndex < SubMeshes.size(); ++SubMeshIndex) {
                const ModelSubMesh& SubMesh{ SubMeshes[SubMeshIndex] };
                const Interface::IPipeline* Pipeline{ nullptr };
                std::uint32_t ResolvedMaterialIndex{ 0 };
                std::uint32_t ResolvedMaterialGroupIndex{ SkinnedMeshRendererComponent.materialGroupIndex };

                if (MaterialGroups.empty() == false && (ResolvedMaterialGroupIndex >= MaterialGroups.size() || MaterialGroups[ResolvedMaterialGroupIndex].Items.empty())) {
                    ResolvedMaterialGroupIndex = 0;
                }

                if (MaterialGroups.empty() == false && ResolvedMaterialGroupIndex < MaterialGroups.size()) {
                    const RegisteredMaterialGroup& RegisteredGroup{ MaterialGroups[ResolvedMaterialGroupIndex] };
                    std::size_t ResolvedItemIndex{ SubMesh.MaterialGroupItemIndex };
                    if (ResolvedItemIndex >= RegisteredGroup.Items.size()) {
                        ResolvedItemIndex = 0;
                    }

                    if (ResolvedItemIndex < RegisteredGroup.Items.size()) {
                        const RegisteredMaterialGroupItem& RegisteredGroupItem{ RegisteredGroup.Items[ResolvedItemIndex] };
                        Pipeline = RegisteredGroupItem.Pipeline;
                        ResolvedMaterialIndex = RegisteredGroupItem.MaterialIndex;
                    }
                }

                RFD::DrawRecord DrawRecord{};
                DrawRecord.pso = Pipeline;
                DrawRecord.mesh = &Node;
                DrawRecord.submesh = static_cast<std::uint32_t>(SubMeshIndex);
                DrawRecord.pass = 0;
                DrawRecord.objectIndex = ModelContext.objectID;
                DrawRecord.materialIndex = ResolvedMaterialIndex;
                const std::uint32_t MaterialFlags{ MaterialComponent == nullptr ? 0u : MaterialComponent->Flags };
                const bool IsPickedHierarchy{ IsEntityWithinPickedHierarchy(World, EntityId, Ctx.PickedEntityId) };
                const std::uint32_t PickFlags{ IsPickedHierarchy ? PickedDrawFlagBitMask : 0u };
                DrawRecord.flags = MaterialFlags | PickFlags;
                RenderData.drawRecords.push_back(DrawRecord);
            }
        }
    }
}
