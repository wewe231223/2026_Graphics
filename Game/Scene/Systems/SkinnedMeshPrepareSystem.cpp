#include "SkinnedMeshPrepareSystem.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
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
    struct ResolvedAnimator final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        const Game::Animator* Component{ nullptr };
    };

    struct ResolvedBoneSkinReference final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        const Game::BoneSkinReference* Component{ nullptr };
    };

    struct BoneWorldBoundingBoxUpdate final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        DirectX::BoundingOrientedBox WorldObb{};
    };

    struct PreparedBoundingBoxUpdate final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        DirectX::BoundingOrientedBox LocalObb{};
        DirectX::BoundingOrientedBox WorldObb{};
    };

    struct PreparedSkinnedResult final {
        Game::SkinnedMeshPreparedData PreparedData{};
        std::optional<PreparedBoundingBoxUpdate> ModelBoundingBoxUpdate{};
        std::vector<BoneWorldBoundingBoxUpdate> BoneBoundingBoxUpdates{};
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

    bool TryResolveWorldMatrix(Arche::World& World, Arche::EntityID EntityId, SimpleMath::Matrix& OutWorldMatrix) {
        const Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(EntityId) };
        if (TransformComponent == nullptr) {
            return false;
        }

        OutWorldMatrix = TransformComponent->worldMatrix;
        return true;
    }

    void GatherBonePoseAndBoundingRecursive(Arche::World& World, Arche::EntityID EntityId, Game::Model* ModelData, std::uint32_t SkinArrayIndex, const SimpleMath::Matrix& MeshWorldInverseMatrix, std::vector<SimpleMath::Matrix>& InOutBoneMatrices, std::optional<DirectX::BoundingBox>& InOutMergedAabb, std::vector<Game::RFD::BoundingBoxContext>& InOutBoundingBoxContexts, std::vector<BoneWorldBoundingBoxUpdate>& InOutBoneBoundingBoxUpdates) {
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
            const bool IsBoneWorldMatrixResolved{ TryResolveWorldMatrix(World, EntityId, BoneWorldMatrix) };
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

                const Game::BoundingBox* BoundingBoxComponent{ World.GetComponent<Game::BoundingBox>(EntityId) };
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
                    InOutBoneBoundingBoxUpdates.push_back(BoneWorldBoundingBoxUpdate{ EntityId, BoneWorldObb });
                }
            }
        }

        Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
        while (ChildEntityId != Arche::NullEntityID) {
            const Game::EntityHierarchy* ChildHierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(ChildEntityId) };
            if (ChildHierarchyComponent == nullptr) {
                break;
            }

            GatherBonePoseAndBoundingRecursive(World, ChildEntityId, ModelData, SkinArrayIndex, MeshWorldInverseMatrix, InOutBoneMatrices, InOutMergedAabb, InOutBoundingBoxContexts, InOutBoneBoundingBoxUpdates);
            ChildEntityId = ChildHierarchyComponent->nextSibling;
        }
    }

    bool TryBuildPreparedResult(Arche::World& World, Arche::EntityID EntityId, PreparedSkinnedResult& OutPreparedResult) {
        const Game::SkinnedMeshRenderer* SkinnedMeshRendererComponent{ World.GetComponent<Game::SkinnedMeshRenderer>(EntityId) };
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
        const bool IsMeshWorldMatrixResolved{ TryResolveWorldMatrix(World, EntityId, MeshWorldMatrix) };
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

        OutPreparedResult.PreparedData.EntityId = EntityId;
        OutPreparedResult.PreparedData.BonePalette.resize(static_cast<std::size_t>(BoneMatrixCount), SimpleMath::Matrix::Identity);

        std::optional<DirectX::BoundingBox> MergedBoneAabb{};
        SimpleMath::Matrix MeshWorldInverseMatrix{ MeshWorldMatrix };
        MeshWorldInverseMatrix = MeshWorldInverseMatrix.Invert();
        GatherBonePoseAndBoundingRecursive(World, BoneRootEntityId, SkinnedMeshRendererComponent->model, SkinArrayIndex, MeshWorldInverseMatrix, OutPreparedResult.PreparedData.BonePalette, MergedBoneAabb, OutPreparedResult.PreparedData.BoneBoundingBoxContexts, OutPreparedResult.BoneBoundingBoxUpdates);

        if (MergedBoneAabb.has_value() == true) {
            DirectX::BoundingOrientedBox MergedBoneObb{};
            DirectX::BoundingOrientedBox::CreateFromBoundingBox(MergedBoneObb, *MergedBoneAabb);
            OutPreparedResult.ModelBoundingBoxUpdate = PreparedBoundingBoxUpdate{ EntityId, MergedBoneObb, MergedBoneObb };
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
        static std::array<ComponentAccess, 6> Accesses{ { { typeid(Animator), Access::Read }, { typeid(BoneSkinReference), Access::Read }, { typeid(SkinnedMeshRenderer), Access::Read }, { typeid(Transform), Access::Read }, { typeid(EntityHierarchy), Access::Read }, { typeid(BoundingBox), Access::Write } } };
        return Accesses;
    }

    std::span<const ResourceAccess> SkinnedMeshPrepareSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 1> Accesses{ { { typeid(std::vector<SkinnedMeshPreparedData>), Access::Write } } };
        return Accesses;
    }

    void SkinnedMeshPrepareSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

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
        std::vector<std::vector<PreparedSkinnedResult>> ThreadLocalPreparedResults{};
        ThreadLocalPreparedResults.resize(ThreadLocalCount);
        Ctx.SkinnedMeshPreparedDataItems.reserve(Ctx.SkinnedMeshPreparedDataItems.size() + TargetEntityIds.size());

        RunParallelBlocks(TargetEntityIds.size(), [&](std::size_t BlockStart, std::size_t BlockEnd) {
            const std::size_t ThreadLocalIndex{ ResolveThreadLocalIndex(ThreadLocalCount) };
            std::vector<PreparedSkinnedResult>& LocalPreparedResults{ ThreadLocalPreparedResults[ThreadLocalIndex] };

            for (std::size_t EntityIndex{ BlockStart }; EntityIndex < BlockEnd; ++EntityIndex) {
                PreparedSkinnedResult PreparedResult{};
                const bool IsPrepared{ TryBuildPreparedResult(World, TargetEntityIds[EntityIndex], PreparedResult) };
                if (IsPrepared == true) {
                    LocalPreparedResults.push_back(std::move(PreparedResult));
                }
            }
        });

        for (std::vector<PreparedSkinnedResult>& LocalPreparedResults : ThreadLocalPreparedResults) {
            for (PreparedSkinnedResult& PreparedResult : LocalPreparedResults) {
                Ctx.SkinnedMeshPreparedDataItems.push_back(std::move(PreparedResult.PreparedData));

                if (PreparedResult.ModelBoundingBoxUpdate.has_value() == true) {
                    BoundingBox* BoundingBoxComponent{ World.GetComponent<BoundingBox>(PreparedResult.ModelBoundingBoxUpdate->EntityId) };
                    if (BoundingBoxComponent != nullptr) {
                        BoundingBoxComponent->SetObb(PreparedResult.ModelBoundingBoxUpdate->LocalObb);
                        BoundingBoxComponent->SetWorldObb(PreparedResult.ModelBoundingBoxUpdate->WorldObb);
                    }
                }

                for (const BoneWorldBoundingBoxUpdate& BoneBoundingBoxUpdate : PreparedResult.BoneBoundingBoxUpdates) {
                    BoundingBox* BoundingBoxComponent{ World.GetComponent<BoundingBox>(BoneBoundingBoxUpdate.EntityId) };
                    if (BoundingBoxComponent != nullptr) {
                        BoundingBoxComponent->SetWorldObb(BoneBoundingBoxUpdate.WorldObb);
                    }
                }
            }
        }
    }
}
