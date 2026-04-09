#include "SkinnedMeshRenderSystem.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

#include "Game/Base/Common.h"
#include "Game/Model/AssetRegistry.h"
#include "Game/Model/Model.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/SkinnedMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    constexpr std::uint32_t SkinnedModelContextFlagBitMask{ 0x1u };
    constexpr std::uint32_t PickedDrawFlagBitMask{ 0x1u };

    struct ResolvedAnimator final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        const Game::Animator* Component{ nullptr };
    };

    struct ResolvedBoneSkinReference final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        const Game::BoneSkinReference* Component{ nullptr };
    };

    struct PendingDrawRecord final {
        const Interface::IPipeline* Pipeline{ nullptr };
        const Interface::IModelNode* Mesh{ nullptr };
        std::uint32_t SubMeshIndex{ 0 };
        std::uint32_t MaterialIndex{ 0 };
        std::uint32_t Flags{ 0 };
    };

    struct SkinnedRenderThreadLocalData final {
        std::vector<Game::RFD::BoundingBoxContext> BoundingBoxContexts{};
        std::vector<SimpleMath::Matrix> BonePalette{};
        std::vector<Game::RFD::ModelContext> ModelContexts{};
        std::vector<std::vector<PendingDrawRecord>> PendingDrawRecordsByModel{};
    };

    struct BoundingBoxUpdate final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        DirectX::BoundingOrientedBox LocalObb{};
        DirectX::BoundingOrientedBox WorldObb{};
    };

    struct BoneWorldBoundingBoxUpdate final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        DirectX::BoundingOrientedBox WorldObb{};
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

    bool TryResolveWorldMatrix(Arche::World& World, Arche::EntityID EntityId, SimpleMath::Matrix& OutWorldMatrix) {
        const Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(EntityId) };
        if (TransformComponent == nullptr) {
            return false;
        }

        OutWorldMatrix = TransformComponent->worldMatrix;
        return true;
    }

    void GatherBonePoseAndBoundingRecursive(Arche::World& World, Arche::EntityID EntityId, Game::Model* ModelData, std::uint32_t SkinArrayIndex, const SimpleMath::Matrix& MeshWorldInverseMatrix, std::vector<SimpleMath::Matrix>* InOutBoneMatrices, std::optional<DirectX::BoundingBox>* InOutMergedAabb, std::vector<Game::RFD::BoundingBoxContext>* InOutBoundingBoxContexts, std::vector<BoneWorldBoundingBoxUpdate>* InOutBoneBoundingBoxUpdates) {
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

                    if (InOutBoneMatrices != nullptr && RuntimeBoneInfoItem.JointArrayIndex < InOutBoneMatrices->size()) {
                        (*InOutBoneMatrices)[RuntimeBoneInfoItem.JointArrayIndex] = RuntimeBoneInfoItem.InverseBindMatrix * BoneWorldMatrix * MeshWorldInverseMatrix;
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
                    if (InOutMergedAabb != nullptr) {
                        if (InOutMergedAabb->has_value() == true) {
                            DirectX::BoundingBox::CreateMerged(**InOutMergedAabb, **InOutMergedAabb, BoneWorldAabb);
                        }
                        else {
                            *InOutMergedAabb = BoneWorldAabb;
                        }
                    }

                    if (InOutBoundingBoxContexts != nullptr) {
                        Game::RFD::BoundingBoxContext BoneBoundingBoxContext{};
                        BoneBoundingBoxContext.center = SimpleMath::Vector4{ BoneWorldObb.Center.x, BoneWorldObb.Center.y, BoneWorldObb.Center.z, 1.0f };
                        BoneBoundingBoxContext.extents = SimpleMath::Vector4{ BoneWorldObb.Extents.x, BoneWorldObb.Extents.y, BoneWorldObb.Extents.z, 0.0f };
                        BoneBoundingBoxContext.orientation = SimpleMath::Vector4{ BoneWorldObb.Orientation.x, BoneWorldObb.Orientation.y, BoneWorldObb.Orientation.z, BoneWorldObb.Orientation.w };
                        InOutBoundingBoxContexts->push_back(BoneBoundingBoxContext);
                    }

                    if (InOutBoneBoundingBoxUpdates != nullptr) {
                        InOutBoneBoundingBoxUpdates->push_back(BoneWorldBoundingBoxUpdate{ EntityId, BoneWorldObb });
                    }
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

    bool BuildEntityRenderData(Arche::World& World, const std::vector<Game::RegisteredMaterialGroup>& MaterialGroups, Arche::EntityID PickedEntityId, Arche::EntityID EntityId, SkinnedRenderThreadLocalData& OutThreadLocalData, std::vector<BoundingBoxUpdate>& OutBoundingBoxUpdates, std::vector<BoneWorldBoundingBoxUpdate>& OutBoneBoundingBoxUpdates) {
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

        std::vector<SimpleMath::Matrix> BoneMatrices{};
        BoneMatrices.resize(static_cast<std::size_t>(BoneMatrixCount), SimpleMath::Matrix::Identity);
        std::optional<DirectX::BoundingBox> MergedBoneAabb{};
        std::vector<Game::RFD::BoundingBoxContext> BoneBoundingBoxContexts{};
        SimpleMath::Matrix MeshWorldInverseMatrix{ MeshWorldMatrix };
        MeshWorldInverseMatrix = MeshWorldInverseMatrix.Invert();
        GatherBonePoseAndBoundingRecursive(World, BoneRootEntityId, SkinnedMeshRendererComponent->model, SkinArrayIndex, MeshWorldInverseMatrix, &BoneMatrices, &MergedBoneAabb, &BoneBoundingBoxContexts, &OutBoneBoundingBoxUpdates);

        const std::vector<Game::ModelSubMesh>& SubMeshes{ Node.GetSubMeshes() };
        if (SubMeshes.empty() == true || BoneMatrices.empty() == true) {
            return false;
        }

        const std::uint32_t ThreadLocalBoneIndexStart{ static_cast<std::uint32_t>(OutThreadLocalData.BonePalette.size()) };
        OutThreadLocalData.BonePalette.insert(OutThreadLocalData.BonePalette.end(), BoneMatrices.begin(), BoneMatrices.end());
        OutThreadLocalData.BoundingBoxContexts.insert(OutThreadLocalData.BoundingBoxContexts.end(), BoneBoundingBoxContexts.begin(), BoneBoundingBoxContexts.end());

        Game::RFD::ModelContext ModelContext{};
        ModelContext.world = MeshWorldMatrix;
        ModelContext.prevWorld = ModelContext.world;
        ModelContext.flags = SkinnedModelContextFlagBitMask;
        ModelContext.boneIndexStart = ThreadLocalBoneIndexStart;
        ModelContext.objectID = static_cast<std::uint32_t>(OutThreadLocalData.ModelContexts.size());
        OutThreadLocalData.ModelContexts.push_back(ModelContext);

        if (MergedBoneAabb.has_value() == true) {
            DirectX::BoundingOrientedBox MergedBoneObb{};
            DirectX::BoundingOrientedBox::CreateFromBoundingBox(MergedBoneObb, *MergedBoneAabb);
            OutBoundingBoxUpdates.push_back(BoundingBoxUpdate{ EntityId, MergedBoneObb, MergedBoneObb });
            Game::RFD::BoundingBoxContext BoundingBoxContext{};
            BoundingBoxContext.center = SimpleMath::Vector4{ MergedBoneObb.Center.x, MergedBoneObb.Center.y, MergedBoneObb.Center.z, 1.0f };
            BoundingBoxContext.extents = SimpleMath::Vector4{ MergedBoneObb.Extents.x, MergedBoneObb.Extents.y, MergedBoneObb.Extents.z, 0.0f };
            BoundingBoxContext.orientation = SimpleMath::Vector4{ MergedBoneObb.Orientation.x, MergedBoneObb.Orientation.y, MergedBoneObb.Orientation.z, MergedBoneObb.Orientation.w };
            OutThreadLocalData.BoundingBoxContexts.push_back(BoundingBoxContext);
        }
        else {
            const Game::BoundingBox* BoundingBoxComponent{ World.GetComponent<Game::BoundingBox>(EntityId) };
            if (BoundingBoxComponent != nullptr && BoundingBoxComponent->HasWorldObb() == true) {
                const DirectX::BoundingOrientedBox& WorldObb{ BoundingBoxComponent->GetWorldObb() };
                Game::RFD::BoundingBoxContext BoundingBoxContext{};
                BoundingBoxContext.center = SimpleMath::Vector4{ WorldObb.Center.x, WorldObb.Center.y, WorldObb.Center.z, 1.0f };
                BoundingBoxContext.extents = SimpleMath::Vector4{ WorldObb.Extents.x, WorldObb.Extents.y, WorldObb.Extents.z, 0.0f };
                BoundingBoxContext.orientation = SimpleMath::Vector4{ WorldObb.Orientation.x, WorldObb.Orientation.y, WorldObb.Orientation.z, WorldObb.Orientation.w };
                OutThreadLocalData.BoundingBoxContexts.push_back(BoundingBoxContext);
            }
        }

        const Game::Material* MaterialComponent{ World.GetComponent<Game::Material>(EntityId) };
        const std::uint32_t MaterialFlags{ MaterialComponent == nullptr ? 0u : MaterialComponent->Flags };
        const bool IsPickedHierarchy{ IsEntityWithinPickedHierarchy(World, EntityId, PickedEntityId) };
        const std::uint32_t PickFlags{ IsPickedHierarchy ? PickedDrawFlagBitMask : 0u };
        const Game::RegisteredMaterialGroup* ResolvedMaterialGroup{ nullptr };
        if (MaterialGroups.empty() == false) {
            std::size_t ResolvedMaterialGroupIndex{ MaterialComponent == nullptr ? 0u : MaterialComponent->MaterialGroupIndex };
            if (ResolvedMaterialGroupIndex >= MaterialGroups.size() || MaterialGroups[ResolvedMaterialGroupIndex].Items.empty()) {
                ResolvedMaterialGroupIndex = 0;
            }

            if (ResolvedMaterialGroupIndex < MaterialGroups.size() && MaterialGroups[ResolvedMaterialGroupIndex].Items.empty() == false) {
                ResolvedMaterialGroup = &MaterialGroups[ResolvedMaterialGroupIndex];
            }
        }

        std::vector<PendingDrawRecord> PendingDrawRecords{};
        PendingDrawRecords.reserve(SubMeshes.size());
        for (std::size_t SubMeshIndex{ 0 }; SubMeshIndex < SubMeshes.size(); ++SubMeshIndex) {
            const Game::ModelSubMesh& SubMesh{ SubMeshes[SubMeshIndex] };
            const Interface::IPipeline* Pipeline{ nullptr };
            std::uint32_t ResolvedMaterialIndex{ 0 };

            if (ResolvedMaterialGroup != nullptr) {
                std::size_t ResolvedItemIndex{ SubMesh.MaterialGroupItemIndex };
                if (ResolvedItemIndex >= ResolvedMaterialGroup->Items.size()) {
                    ResolvedItemIndex = 0;
                }

                if (ResolvedItemIndex < ResolvedMaterialGroup->Items.size()) {
                    const Game::RegisteredMaterialGroupItem& RegisteredGroupItem{ ResolvedMaterialGroup->Items[ResolvedItemIndex] };
                    Pipeline = RegisteredGroupItem.Pipeline;
                    ResolvedMaterialIndex = RegisteredGroupItem.MaterialIndex;
                }
            }

            PendingDrawRecords.push_back(PendingDrawRecord{ Pipeline, &Node, static_cast<std::uint32_t>(SubMeshIndex), ResolvedMaterialIndex, MaterialFlags | PickFlags });
        }

        OutThreadLocalData.PendingDrawRecordsByModel.push_back(std::move(PendingDrawRecords));
        return true;
    }
}

namespace Game {
    SkinnedMeshRenderSystem::SkinnedMeshRenderSystem()
        : mThreadPool{} {
    }

    const std::string& SkinnedMeshRenderSystem::Name() const {
        return mName;
    }

    Phase SkinnedMeshRenderSystem::GetPhase() const {
        return Phase::Render;
    }

    std::span<const ComponentAccess> SkinnedMeshRenderSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 6> Accesses{ { { typeid(Animator), Access::Read }, { typeid(BoneSkinReference), Access::Read }, { typeid(SkinnedMeshRenderer), Access::Read }, { typeid(Transform), Access::Read }, { typeid(EntityHierarchy), Access::Read }, { typeid(BoundingBox), Access::Write } } };
        return Accesses;
    }

    std::span<const ResourceAccess> SkinnedMeshRenderSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 3> Accesses{ { { typeid(RFD::RenderFrameData), Access::Write }, { typeid(std::vector<RegisteredMaterialGroup>), Access::Read }, { typeid(Arche::EntityID), Access::Read } } };
        return Accesses;
    }

    void SkinnedMeshRenderSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        RFD::RenderFrameData& RenderData{ Ctx.RenderData };
        const std::vector<RegisteredMaterialGroup>& MaterialGroups{ *Ctx.MaterialGroups };
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

        const std::size_t WorkerThreadCount{ mThreadPool.get_thread_count() };
        const std::size_t ThreadLocalCount{ WorkerThreadCount == 0 ? 1 : WorkerThreadCount };
        std::vector<SkinnedRenderThreadLocalData> ThreadLocalData{};
        ThreadLocalData.resize(ThreadLocalCount);

        std::vector<BoundingBoxUpdate> BoundingBoxUpdates{};
        std::vector<BoneWorldBoundingBoxUpdate> BoneWorldBoundingBoxUpdates{};
        std::mutex BoundingBoxUpdatesMutex{};
        std::mutex BoneWorldBoundingBoxUpdatesMutex{};
        auto BuildBlocks = [&](const std::size_t BlockStart, const std::size_t BlockEnd) -> void {
            std::size_t ThreadLocalIndex{ 0 };
            const std::optional<std::size_t> WorkerIndex{ BS::this_thread::get_index() };
            if (WorkerIndex.has_value() == true && WorkerIndex.value() < ThreadLocalData.size()) {
                ThreadLocalIndex = WorkerIndex.value();
            }

            SkinnedRenderThreadLocalData& LocalData{ ThreadLocalData[ThreadLocalIndex] };
            std::vector<BoundingBoxUpdate> LocalBoundingBoxUpdates{};
            std::vector<BoneWorldBoundingBoxUpdate> LocalBoneWorldBoundingBoxUpdates{};
            for (std::size_t EntityIndex{ BlockStart }; EntityIndex < BlockEnd; ++EntityIndex) {
                BuildEntityRenderData(World, MaterialGroups, Ctx.PickedEntityId, TargetEntityIds[EntityIndex], LocalData, LocalBoundingBoxUpdates, LocalBoneWorldBoundingBoxUpdates);
            }

            if (LocalBoundingBoxUpdates.empty() == false) {
                std::lock_guard<std::mutex> Lock{ BoundingBoxUpdatesMutex };
                BoundingBoxUpdates.insert(BoundingBoxUpdates.end(), LocalBoundingBoxUpdates.begin(), LocalBoundingBoxUpdates.end());
            }

            if (LocalBoneWorldBoundingBoxUpdates.empty() == false) {
                std::lock_guard<std::mutex> Lock{ BoneWorldBoundingBoxUpdatesMutex };
                BoneWorldBoundingBoxUpdates.insert(BoneWorldBoundingBoxUpdates.end(), LocalBoneWorldBoundingBoxUpdates.begin(), LocalBoneWorldBoundingBoxUpdates.end());
            }
        };

        BS::multi_future<void> BuildFutures{ mThreadPool.submit_blocks<std::size_t>(0, TargetEntityIds.size(), BuildBlocks) };
        BuildFutures.wait();

        for (const BoundingBoxUpdate& BoundingBoxUpdateItem : BoundingBoxUpdates) {
            BoundingBox* BoundingBoxComponent{ World.GetComponent<BoundingBox>(BoundingBoxUpdateItem.EntityId) };
            if (BoundingBoxComponent == nullptr) {
                continue;
            }

            BoundingBoxComponent->SetObb(BoundingBoxUpdateItem.LocalObb);
            BoundingBoxComponent->SetWorldObb(BoundingBoxUpdateItem.WorldObb);
        }

        for (const BoneWorldBoundingBoxUpdate& BoneWorldBoundingBoxUpdateItem : BoneWorldBoundingBoxUpdates) {
            BoundingBox* BoundingBoxComponent{ World.GetComponent<BoundingBox>(BoneWorldBoundingBoxUpdateItem.EntityId) };
            if (BoundingBoxComponent == nullptr) {
                continue;
            }

            BoundingBoxComponent->SetWorldObb(BoneWorldBoundingBoxUpdateItem.WorldObb);
        }

        for (const SkinnedRenderThreadLocalData& ThreadLocalDataItem : ThreadLocalData) {
            const std::uint32_t GlobalBoneIndexStart{ static_cast<std::uint32_t>(RenderData.bonePalette.size()) };
            const std::uint32_t GlobalModelIndexStart{ static_cast<std::uint32_t>(RenderData.modelContexts.size()) };
            RenderData.bonePalette.insert(RenderData.bonePalette.end(), ThreadLocalDataItem.BonePalette.begin(), ThreadLocalDataItem.BonePalette.end());
            RenderData.boundingBoxContexts.insert(RenderData.boundingBoxContexts.end(), ThreadLocalDataItem.BoundingBoxContexts.begin(), ThreadLocalDataItem.BoundingBoxContexts.end());

            for (std::size_t ModelIndex{ 0 }; ModelIndex < ThreadLocalDataItem.ModelContexts.size(); ++ModelIndex) {
                RFD::ModelContext ModelContext{ ThreadLocalDataItem.ModelContexts[ModelIndex] };
                ModelContext.boneIndexStart += GlobalBoneIndexStart;
                ModelContext.objectID = GlobalModelIndexStart + static_cast<std::uint32_t>(ModelIndex);
                RenderData.modelContexts.push_back(ModelContext);

                if (ModelIndex >= ThreadLocalDataItem.PendingDrawRecordsByModel.size()) {
                    continue;
                }

                const std::vector<PendingDrawRecord>& PendingDrawRecords{ ThreadLocalDataItem.PendingDrawRecordsByModel[ModelIndex] };
                for (const PendingDrawRecord& PendingDrawRecordItem : PendingDrawRecords) {
                    RFD::DrawRecord DrawRecord{};
                    DrawRecord.pso = PendingDrawRecordItem.Pipeline;
                    DrawRecord.mesh = PendingDrawRecordItem.Mesh;
                    DrawRecord.submesh = PendingDrawRecordItem.SubMeshIndex;
                    DrawRecord.pass = 0;
                    DrawRecord.objectIndex = ModelContext.objectID;
                    DrawRecord.materialIndex = PendingDrawRecordItem.MaterialIndex;
                    DrawRecord.flags = PendingDrawRecordItem.Flags;
                    RenderData.drawRecords.push_back(DrawRecord);
                }
            }
        }
    }
}
