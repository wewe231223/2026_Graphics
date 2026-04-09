#include "SkinnedMeshPrepareSystem.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Game/Base/Common.h"
#include "Game/Model/Model.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/BoundingBox.h"
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

            const Game::Transform* TransformComponent{ std::as_const(World).GetComponent<Game::Transform>(CurrentEntityId) };
            const Game::EntityHierarchy* HierarchyComponent{ std::as_const(World).GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (TransformComponent == nullptr || HierarchyComponent == nullptr) {
                return false;
            }

            EntityPath.push_back(CurrentEntityId);
            CurrentEntityId = HierarchyComponent->parent;
        }

        for (std::vector<Arche::EntityID>::const_reverse_iterator EntityPathIter{ EntityPath.crbegin() }; EntityPathIter != EntityPath.crend(); ++EntityPathIter) {
            const Arche::EntityID CurrentPathEntityId{ *EntityPathIter };
            const Game::Transform* TransformComponent{ std::as_const(World).GetComponent<Game::Transform>(CurrentPathEntityId) };
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

    void UpdateWorldMatrices(Arche::World& World) {
        std::unordered_map<Arche::EntityID, SimpleMath::Matrix> WorldMatrices{};
        for (auto [TransformComponent, HierarchyComponent] : World.Query<Game::Transform, Game::EntityHierarchy>()) {
            const Arche::EntityID EntityId{ HierarchyComponent.self };
            SimpleMath::Matrix WorldMatrix{};
            const bool IsWorldMatrixResolved{ TryResolveWorldMatrix(World, EntityId, WorldMatrices, WorldMatrix) };
            if (IsWorldMatrixResolved == false) {
                continue;
            }

            TransformComponent.worldMatrix = WorldMatrix;
        }
    }

    struct ResolvedAnimator final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        const Game::Animator* Component{ nullptr };
    };

    struct ResolvedBoneSkinReference final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        const Game::BoneSkinReference* Component{ nullptr };
    };

    ResolvedAnimator ResolveAnimatorInHierarchy(Arche::World& World, Arche::EntityID StartEntityId) {
        Arche::EntityID CurrentEntityId{ StartEntityId };
        while (CurrentEntityId != Arche::NullEntityID) {
            const Game::Animator* AnimatorComponent{ std::as_const(World).GetComponent<Game::Animator>(CurrentEntityId) };
            if (AnimatorComponent != nullptr) {
                return ResolvedAnimator{ CurrentEntityId, AnimatorComponent };
            }

            const Game::EntityHierarchy* HierarchyComponent{ std::as_const(World).GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
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
            const Game::BoneSkinReference* BoneSkinReferenceComponent{ std::as_const(World).GetComponent<Game::BoneSkinReference>(CurrentEntityId) };
            if (BoneSkinReferenceComponent != nullptr) {
                return ResolvedBoneSkinReference{ CurrentEntityId, BoneSkinReferenceComponent };
            }

            const Game::EntityHierarchy* HierarchyComponent{ std::as_const(World).GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (HierarchyComponent == nullptr) {
                break;
            }

            CurrentEntityId = HierarchyComponent->parent;
        }

        return ResolvedBoneSkinReference{};
    }

    bool TryResolveTransformWorldMatrix(Arche::World& World, Arche::EntityID EntityId, SimpleMath::Matrix& OutWorldMatrix) {
        const Game::Transform* TransformComponent{ std::as_const(World).GetComponent<Game::Transform>(EntityId) };
        if (TransformComponent == nullptr) {
            return false;
        }

        OutWorldMatrix = TransformComponent->worldMatrix;
        return true;
    }

    void GatherBonePoseAndBoundingRecursive(Arche::World& World, Arche::EntityID EntityId, Game::Model* ModelData, std::uint32_t SkinArrayIndex, const SimpleMath::Matrix& MeshWorldInverseMatrix, std::vector<SimpleMath::Matrix>& InOutBoneMatrices, std::optional<DirectX::BoundingBox>& InOutMergedAabb, std::vector<Game::RFD::BoundingBoxContext>& InOutBoundingBoxContexts) {
        if (EntityId == Arche::NullEntityID || ModelData == nullptr) {
            return;
        }

        const Game::EntityHierarchy* HierarchyComponent{ std::as_const(World).GetComponent<Game::EntityHierarchy>(EntityId) };
        if (HierarchyComponent == nullptr) {
            return;
        }

        const Game::Bone* BoneComponent{ std::as_const(World).GetComponent<Game::Bone>(EntityId) };
        if (BoneComponent != nullptr && BoneComponent->model == ModelData) {
            SimpleMath::Matrix BoneWorldMatrix{};
            const bool IsBoneWorldMatrixResolved{ TryResolveTransformWorldMatrix(World, EntityId, BoneWorldMatrix) };
            if (IsBoneWorldMatrixResolved == true) {
                const std::span<const Game::RuntimeBoneInfo> RuntimeBoneInfos{ ModelData->GetRuntimeBoneInfos(BoneComponent->runtimeBoneInfoOffset, BoneComponent->runtimeBoneInfoCount) };
                for (const Game::RuntimeBoneInfo& RuntimeBoneInfoItem : RuntimeBoneInfos) {
                    if (RuntimeBoneInfoItem.SkinArrayIndex != SkinArrayIndex) {
                        continue;
                    }

                    if (RuntimeBoneInfoItem.JointArrayIndex < InOutBoneMatrices.size()) {
                        InOutBoneMatrices[RuntimeBoneInfoItem.JointArrayIndex] = RuntimeBoneInfoItem.InverseBindMatrix * BoneWorldMatrix * MeshWorldInverseMatrix;
                    }
                }

                const Game::BoundingBox* BoundingBoxComponent{ std::as_const(World).GetComponent<Game::BoundingBox>(EntityId) };
                if (BoundingBoxComponent != nullptr) {
                    DirectX::BoundingOrientedBox BoneWorldObb{};
                    BoundingBoxComponent->GetObb().Transform(BoneWorldObb, BoneWorldMatrix);
                    DirectX::BoundingBox BoneWorldAabb{};
                    std::array<SimpleMath::Vector3, 8> BoneCorners{};
                    BoneWorldObb.GetCorners(reinterpret_cast<DirectX::XMFLOAT3*>(BoneCorners.data()));
                    DirectX::BoundingBox::CreateFromPoints(BoneWorldAabb, BoneCorners.size(), reinterpret_cast<const DirectX::XMFLOAT3*>(BoneCorners.data()), sizeof(SimpleMath::Vector3));
                    if (InOutMergedAabb.has_value() == true) {
                        DirectX::BoundingBox::CreateMerged(*InOutMergedAabb, *InOutMergedAabb, BoneWorldAabb);
                    }
                    else {
                        InOutMergedAabb = BoneWorldAabb;
                    }

                    Game::RFD::BoundingBoxContext BoneBoundingBoxContext{};
                    BoneBoundingBoxContext.center = SimpleMath::Vector4{ BoneWorldObb.Center.x, BoneWorldObb.Center.y, BoneWorldObb.Center.z, 1.0f };
                    BoneBoundingBoxContext.extents = SimpleMath::Vector4{ BoneWorldObb.Extents.x, BoneWorldObb.Extents.y, BoneWorldObb.Extents.z, 0.0f };
                    BoneBoundingBoxContext.orientation = SimpleMath::Vector4{ BoneWorldObb.Orientation.x, BoneWorldObb.Orientation.y, BoneWorldObb.Orientation.z, BoneWorldObb.Orientation.w };
                    InOutBoundingBoxContexts.push_back(BoneBoundingBoxContext);
                    Game::BoundingBox* BoneBoundingBoxComponent{ World.GetComponent<Game::BoundingBox>(EntityId) };
                    if (BoneBoundingBoxComponent != nullptr) {
                        BoneBoundingBoxComponent->SetWorldObb(BoneWorldObb);
                    }
                }
            }
        }

        Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
        while (ChildEntityId != Arche::NullEntityID) {
            const Game::EntityHierarchy* ChildHierarchyComponent{ std::as_const(World).GetComponent<Game::EntityHierarchy>(ChildEntityId) };
            if (ChildHierarchyComponent == nullptr) {
                break;
            }

            GatherBonePoseAndBoundingRecursive(World, ChildEntityId, ModelData, SkinArrayIndex, MeshWorldInverseMatrix, InOutBoneMatrices, InOutMergedAabb, InOutBoundingBoxContexts);
            ChildEntityId = ChildHierarchyComponent->nextSibling;
        }
    }

    bool TryBuildPreparedResult(Arche::World& World, Arche::EntityID EntityId, Game::SkinnedMeshPreparedData& OutPreparedData) {
        const Game::SkinnedMeshRenderer* SkinnedMeshRendererComponent{ std::as_const(World).GetComponent<Game::SkinnedMeshRenderer>(EntityId) };
        if (SkinnedMeshRendererComponent == nullptr || SkinnedMeshRendererComponent->active == false || SkinnedMeshRendererComponent->model == nullptr) {
            return false;
        }

        const ResolvedAnimator ResolvedAnimatorComponent{ ResolveAnimatorInHierarchy(World, EntityId) };
        if (ResolvedAnimatorComponent.Component == nullptr) {
            return false;
        }

        const ResolvedBoneSkinReference ResolvedBoneSkinReferenceComponent{ ResolveBoneSkinReferenceInHierarchy(World, EntityId) };
        if (ResolvedBoneSkinReferenceComponent.Component == nullptr) {
            return false;
        }

        const Arche::EntityID BoneRootEntityId{ ResolvedBoneSkinReferenceComponent.Component->boneRootEntityId };
        if (BoneRootEntityId == Arche::NullEntityID) {
            return false;
        }

        SimpleMath::Matrix MeshWorldMatrix{};
        const bool IsMeshWorldMatrixResolved{ TryResolveTransformWorldMatrix(World, EntityId, MeshWorldMatrix) };
        if (IsMeshWorldMatrixResolved == false) {
            return false;
        }

        const std::vector<Game::ModelNode>& Nodes{ SkinnedMeshRendererComponent->model->GetNodes() };
        if (SkinnedMeshRendererComponent->nodeIndex >= Nodes.size()) {
            return false;
        }

        const Game::ModelNode& Node{ Nodes[SkinnedMeshRendererComponent->nodeIndex] };
        const std::uint32_t SkinArrayIndex{ Node.GetId() };

        std::uint32_t BoneMatrixCount{ 0 };
        const bool IsBoneMatrixCountResolved{ SkinnedMeshRendererComponent->model->TryGetRuntimeBoneMatrixCount(SkinArrayIndex, BoneMatrixCount) };
        if (IsBoneMatrixCountResolved == false || BoneMatrixCount == 0) {
            return false;
        }

        OutPreparedData.EntityId = EntityId;
        OutPreparedData.BonePalette.resize(static_cast<std::size_t>(BoneMatrixCount), SimpleMath::Matrix::Identity);

        std::optional<DirectX::BoundingBox> MergedBoneAabb{};
        SimpleMath::Matrix MeshWorldInverseMatrix{ MeshWorldMatrix };
        MeshWorldInverseMatrix = MeshWorldInverseMatrix.Invert();
        GatherBonePoseAndBoundingRecursive(World, BoneRootEntityId, SkinnedMeshRendererComponent->model, SkinArrayIndex, MeshWorldInverseMatrix, OutPreparedData.BonePalette, MergedBoneAabb, OutPreparedData.BoneBoundingBoxContexts);

        if (MergedBoneAabb.has_value() == true) {
            DirectX::BoundingOrientedBox MergedBoneObb{};
            DirectX::BoundingOrientedBox::CreateFromBoundingBox(MergedBoneObb, *MergedBoneAabb);
            Game::BoundingBox* ModelBoundingBoxComponent{ World.GetComponent<Game::BoundingBox>(EntityId) };
            if (ModelBoundingBoxComponent != nullptr) {
                ModelBoundingBoxComponent->SetObb(MergedBoneObb);
                ModelBoundingBoxComponent->SetWorldObb(MergedBoneObb);
            }
        }

        return true;
    }
}

namespace Game {
    SkinnedMeshPrepareSystem::SkinnedMeshPrepareSystem()
        : ParallelSystemBase{} {
    }

    SkinnedMeshPrepareSystem::~SkinnedMeshPrepareSystem() {
    }

    const std::string& SkinnedMeshPrepareSystem::Name() const {
        return mName;
    }

    Phase SkinnedMeshPrepareSystem::GetPhase() const {
        return Phase::RenderPrepare;
    }

    std::span<const ComponentAccess> SkinnedMeshPrepareSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 6> Accesses{ { { typeid(Animator), Access::Read }, { typeid(BoneSkinReference), Access::Read }, { typeid(SkinnedMeshRenderer), Access::Read }, { typeid(Transform), Access::Write }, { typeid(EntityHierarchy), Access::Read }, { typeid(BoundingBox), Access::Write } } };
        return Accesses;
    }

    std::span<const ResourceAccess> SkinnedMeshPrepareSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 1> Accesses{ { { typeid(std::vector<SkinnedMeshPreparedData>), Access::Write } } };
        return Accesses;
    }

    void SkinnedMeshPrepareSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        UpdateWorldMatrices(World);

        std::vector<Arche::EntityID> TargetEntityIds{};
        for (auto [SkinnedMeshRendererComponent, EntityHierarchyComponent] : World.Query<SkinnedMeshRenderer, EntityHierarchy>()) {
            if (SkinnedMeshRendererComponent.active == false || SkinnedMeshRendererComponent.model == nullptr) {
                continue;
            }

            TargetEntityIds.push_back(EntityHierarchyComponent.self);
        }

        if (TargetEntityIds.empty() == true) {
            return;
        }

        const std::size_t ThreadLocalCount{ GetWorkerThreadCount() };
        std::vector<std::vector<SkinnedMeshPreparedData>> ThreadLocalPreparedDataItems{};
        ThreadLocalPreparedDataItems.resize(ThreadLocalCount);
        Ctx.SkinnedMeshPreparedDataItems.reserve(Ctx.SkinnedMeshPreparedDataItems.size() + TargetEntityIds.size());

        RunParallelBlocks(TargetEntityIds.size(), [&](std::size_t BlockStart, std::size_t BlockEnd) {
            const std::size_t ThreadLocalIndex{ ResolveThreadLocalIndex(ThreadLocalCount) };
            std::vector<SkinnedMeshPreparedData>& LocalPreparedDataItems{ ThreadLocalPreparedDataItems[ThreadLocalIndex] };

            for (std::size_t EntityIndex{ BlockStart }; EntityIndex < BlockEnd; ++EntityIndex) {
                SkinnedMeshPreparedData PreparedData{};
                const bool IsPrepared{ TryBuildPreparedResult(World, TargetEntityIds[EntityIndex], PreparedData) };
                if (IsPrepared == true) {
                    LocalPreparedDataItems.push_back(std::move(PreparedData));
                }
            }
        });

        for (std::vector<SkinnedMeshPreparedData>& LocalPreparedDataItems : ThreadLocalPreparedDataItems) {
            for (SkinnedMeshPreparedData& PreparedData : LocalPreparedDataItems) {
                Ctx.SkinnedMeshPreparedDataItems.push_back(std::move(PreparedData));
            }
        }
    }
}
