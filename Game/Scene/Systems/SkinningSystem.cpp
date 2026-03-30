#include "SkinningSystem.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Game/Model/Model.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/SkinnedMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    SimpleMath::Matrix BuildLocalWorldMatrix(const Game::Transform& TransformComponent) {
        const SimpleMath::Matrix TrsMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
        return TransformComponent.nodeToParent * TrsMatrix;
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

    bool TryResolveWorldPositionFromNodeToParent(Arche::World& World, Arche::EntityID EntityId, SimpleMath::Vector3& OutWorldPosition) {
        std::vector<Arche::EntityID> EntityPath{};
        Arche::EntityID CurrentEntityId{ EntityId };

        while (CurrentEntityId != Arche::NullEntityID) {
            const Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(CurrentEntityId) };
            const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (TransformComponent == nullptr || HierarchyComponent == nullptr) {
                return false;
            }

            EntityPath.push_back(CurrentEntityId);
            CurrentEntityId = HierarchyComponent->parent;
        }

        SimpleMath::Matrix CurrentWorldMatrix{ SimpleMath::Matrix::Identity };
        for (std::vector<Arche::EntityID>::const_reverse_iterator EntityPathIter{ EntityPath.crbegin() }; EntityPathIter != EntityPath.crend(); ++EntityPathIter) {
            const Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(*EntityPathIter) };
            if (TransformComponent == nullptr) {
                return false;
            }

            const SimpleMath::Matrix LocalNodeToParentMatrix{ BuildLocalWorldMatrix(*TransformComponent) };
            CurrentWorldMatrix = LocalNodeToParentMatrix * CurrentWorldMatrix;
        }

        OutWorldPosition = SimpleMath::Vector3{ CurrentWorldMatrix._41, CurrentWorldMatrix._42, CurrentWorldMatrix._43 };
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
}

namespace Game {
    const std::string& SkinningSystem::Name() const {
        return mName;
    }

    Phase SkinningSystem::GetPhase() const {
        return Phase::Skinning;
    }

    std::span<const ComponentAccess> SkinningSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 5> Accesses{ { { typeid(BoneSkinReference), Access::Read }, { typeid(SkinnedMeshRenderer), Access::Read }, { typeid(EntityHierarchy), Access::Read }, { typeid(Bone), Access::Read }, { typeid(Transform), Access::Write } } };
        return Accesses;
    }

    std::span<const ResourceAccess> SkinningSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 2> Accesses{ { { typeid(std::unordered_map<Arche::EntityID, SimpleMath::Matrix>), Access::Write }, { typeid(std::unordered_map<Arche::EntityID, SkinnedPoseCacheEntry>), Access::Write } } };
        return Accesses;
    }

    void SkinningSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        for (auto [BoneComponent, TransformComponent] : World.Query<Bone, Transform>()) {
            (void)BoneComponent;
            TransformComponent.rootBoneWorldPosition = SimpleMath::Vector3::Zero;
            TransformComponent.hasRootBoneWorldPosition = false;
        }

        for (auto [BoneSkinReferenceComponent, SkinnedMeshRendererComponent, EntityHierarchyComponent] : World.Query<BoneSkinReference, SkinnedMeshRenderer, EntityHierarchy>()) {

            if (SkinnedMeshRendererComponent.active == false || SkinnedMeshRendererComponent.model == nullptr) {
                continue;
            }

            if (BoneSkinReferenceComponent.boneRootEntityId == Arche::NullEntityID) {
                continue;
            }

            SimpleMath::Vector3 RootBoneWorldPosition{};
            const bool IsRootBoneWorldPositionResolved{ TryResolveWorldPositionFromNodeToParent(World, BoneSkinReferenceComponent.boneRootEntityId, RootBoneWorldPosition) };
            if (IsRootBoneWorldPositionResolved == true) {
                Transform* RootBoneTransformComponent{ World.GetComponent<Transform>(BoneSkinReferenceComponent.boneRootEntityId) };
                if (RootBoneTransformComponent != nullptr) {
                    RootBoneTransformComponent->rootBoneWorldPosition = RootBoneWorldPosition;
                    RootBoneTransformComponent->hasRootBoneWorldPosition = true;
                }
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
            GatherBoneMatricesRecursive(World, BoneSkinReferenceComponent.boneRootEntityId, SkinnedMeshRendererComponent.model, SkinArrayIndex, MeshWorldInverseMatrix, Ctx.WorldMatrices, BoneMatrices);

            SkinnedPoseCacheEntry CacheEntry{};
            CacheEntry.SkinArrayIndex = SkinArrayIndex;
            CacheEntry.BoneMatrices = std::move(BoneMatrices);
            CacheEntry.IsValid = true;
            Ctx.SkinnedPoseCache[EntityId] = std::move(CacheEntry);
        }
    }
}
