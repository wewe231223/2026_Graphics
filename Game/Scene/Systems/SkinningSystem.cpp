#include "SkinningSystem.h"

#include <array>
#include <cstdint>
#include <vector>

#include "Game/Model/Model.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/SkinnedMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    struct ResolvedBoneSkinReference final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        const Game::BoneSkinReference* Component{ nullptr };
    };

    struct ResolvedAnimator final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        const Game::Animator* Component{ nullptr };
    };

    ResolvedAnimator ResolveAnimatorInHierarchy(Arche::World& World, Arche::EntityID StartEntityId) {
        Arche::EntityID CurrentEntityId{ StartEntityId };
        while (CurrentEntityId != Arche::NullEntityID) {
            const Game::Animator* AnimatorComponent{ World.GetComponent<Game::Animator>(CurrentEntityId) };
            if (AnimatorComponent != nullptr) {
                return ResolvedAnimator{ CurrentEntityId, AnimatorComponent };
            }

            const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (HierarchyComponent == nullptr) {
                break;
            }

            CurrentEntityId = HierarchyComponent->parent;
        }

        return ResolvedAnimator{};
    }

    ResolvedBoneSkinReference ResolveBoneSkinReferenceInHierarchy(Arche::World& World, Arche::EntityID StartEntityId) {
        Arche::EntityID CurrentEntityId{ StartEntityId };
        while (CurrentEntityId != Arche::NullEntityID) {
            const Game::BoneSkinReference* BoneSkinReferenceComponent{ World.GetComponent<Game::BoneSkinReference>(CurrentEntityId) };
            if (BoneSkinReferenceComponent != nullptr) {
                return ResolvedBoneSkinReference{ CurrentEntityId, BoneSkinReferenceComponent };
            }

            const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (HierarchyComponent == nullptr) {
                break;
            }

            CurrentEntityId = HierarchyComponent->parent;
        }

        return ResolvedBoneSkinReference{};
    }

    SimpleMath::Matrix BuildLocalWorldMatrix(const Game::Transform& TransformComponent) {
        const SimpleMath::Matrix TrsMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
        return TransformComponent.nodeToParent * TrsMatrix;
    }

    void GatherBoneMatricesRecursive(Arche::World& World, Arche::EntityID EntityId, Game::Model* ModelData, std::uint32_t SkinArrayIndex, const SimpleMath::Matrix& ParentWorldMatrix, const SimpleMath::Matrix& MeshWorldInverseMatrix, std::vector<SimpleMath::Matrix>& InOutBoneMatrices) {
        if (EntityId == Arche::NullEntityID || ModelData == nullptr) {
            return;
        }

        const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(EntityId) };
        if (HierarchyComponent == nullptr) {
            return;
        }

        const Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(EntityId) };
        if (TransformComponent == nullptr) {
            return;
        }

        const Game::EntityHierarchy* ParentHierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(HierarchyComponent->parent) };
        const SimpleMath::Matrix BoneLocalWorldMatrix{ BuildLocalWorldMatrix(*TransformComponent) };
        const SimpleMath::Matrix BoneWorldMatrix{ HierarchyComponent->parent == Arche::NullEntityID || ParentHierarchyComponent == nullptr ? BoneLocalWorldMatrix : BoneLocalWorldMatrix * ParentWorldMatrix };

        const Game::Bone* BoneComponent{ World.GetComponent<Game::Bone>(EntityId) };
        if (BoneComponent != nullptr && BoneComponent->model == ModelData) {
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

        Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
        while (ChildEntityId != Arche::NullEntityID) {
            const Game::EntityHierarchy* ChildHierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(ChildEntityId) };
            if (ChildHierarchyComponent == nullptr) {
                break;
            }

            GatherBoneMatricesRecursive(World, ChildEntityId, ModelData, SkinArrayIndex, BoneWorldMatrix, MeshWorldInverseMatrix, InOutBoneMatrices);
            ChildEntityId = ChildHierarchyComponent->nextSibling;
        }
    }
}

namespace Game {
    const std::string& SkinningSystem::Name() const {
        return mName;
    }

    Phase SkinningSystem::GetPhase() const {
        return Phase::Skinning;
    }

    std::span<const ComponentAccess> SkinningSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 6> Accesses{ { { typeid(Animator), Access::Read }, { typeid(BoneSkinReference), Access::Read }, { typeid(SkinnedMeshRenderer), Access::Read }, { typeid(EntityHierarchy), Access::Read }, { typeid(Bone), Access::Read }, { typeid(Transform), Access::Read } } };
        return Accesses;
    }

    std::span<const ResourceAccess> SkinningSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 2> Accesses{ { { typeid(std::unordered_map<Arche::EntityID, SimpleMath::Matrix>), Access::Read }, { typeid(std::unordered_map<Arche::EntityID, SkinnedPoseCacheEntry>), Access::Write } } };
        return Accesses;
    }

    void SkinningSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        for (auto [SkinnedMeshRendererComponent, EntityHierarchyComponent] : World.Query<SkinnedMeshRenderer, EntityHierarchy>()) {

            if (SkinnedMeshRendererComponent.active == false || SkinnedMeshRendererComponent.model == nullptr) {
                continue;
            }

            const Arche::EntityID EntityId{ EntityHierarchyComponent.self };
            const ResolvedAnimator ResolvedAnimatorComponent{ ResolveAnimatorInHierarchy(World, EntityId) };
            if (ResolvedAnimatorComponent.Component == nullptr) {
                continue;
            }

            const ResolvedBoneSkinReference ResolvedBoneSkinReferenceComponent{ ResolveBoneSkinReferenceInHierarchy(World, EntityId) };
            if (ResolvedBoneSkinReferenceComponent.Component == nullptr) {
                continue;
            }

            const Arche::EntityID BoneRootEntityId{ ResolvedBoneSkinReferenceComponent.Component->boneRootEntityId };
            if (BoneRootEntityId == Arche::NullEntityID) {
                continue;
            }

            const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator MeshWorldMatrixIter{ Ctx.WorldMatrices.find(EntityId) };
            if (MeshWorldMatrixIter == Ctx.WorldMatrices.end()) {
                continue;
            }

            const SimpleMath::Matrix MeshWorldMatrix{ MeshWorldMatrixIter->second };
            const std::vector<ModelNode>& Nodes{ SkinnedMeshRendererComponent.model->GetNodes() };
            if (SkinnedMeshRendererComponent.nodeIndex >= Nodes.size()) {
                continue;
            }

            const ModelNode& Node{ Nodes[SkinnedMeshRendererComponent.nodeIndex] };
            const std::uint32_t SkinArrayIndex{ Node.GetId() };
            std::uint32_t BoneMatrixCount{ 0 };
            const bool IsBoneMatrixCountResolved{ SkinnedMeshRendererComponent.model->TryGetRuntimeBoneMatrixCount(SkinArrayIndex, BoneMatrixCount) };
            if (IsBoneMatrixCountResolved == false || BoneMatrixCount == 0) {
                continue;
            }

            SimpleMath::Matrix MeshWorldInverseMatrix{ MeshWorldMatrix };
            MeshWorldInverseMatrix = MeshWorldInverseMatrix.Invert();

            std::vector<SimpleMath::Matrix> BoneMatrices{};
            BoneMatrices.resize(static_cast<std::size_t>(BoneMatrixCount), SimpleMath::Matrix::Identity);
            SimpleMath::Matrix BoneRootParentWorldMatrix{ SimpleMath::Matrix::Identity };
            const EntityHierarchy* BoneRootHierarchyComponent{ World.GetComponent<EntityHierarchy>(BoneRootEntityId) };
            if (BoneRootHierarchyComponent != nullptr) {
                const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator BoneRootParentWorldMatrixIter{ Ctx.WorldMatrices.find(BoneRootHierarchyComponent->parent) };
                if (BoneRootParentWorldMatrixIter != Ctx.WorldMatrices.end()) {
                    BoneRootParentWorldMatrix = BoneRootParentWorldMatrixIter->second;
                }
            }

            GatherBoneMatricesRecursive(World, BoneRootEntityId, SkinnedMeshRendererComponent.model, SkinArrayIndex, BoneRootParentWorldMatrix, MeshWorldInverseMatrix, BoneMatrices);

            SkinnedPoseCacheEntry CacheEntry{};
            CacheEntry.SkinArrayIndex = SkinArrayIndex;
            CacheEntry.BoneMatrices = std::move(BoneMatrices);
            CacheEntry.IsValid = true;
            Ctx.SkinnedPoseCache[EntityId] = std::move(CacheEntry);
        }
    }
}
