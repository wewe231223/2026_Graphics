#include "SkinnedMeshRenderSystem.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <unordered_map>
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

    struct JobInput final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        Arche::EntityID BoneRootEntityId{ Arche::NullEntityID };
        const Game::SkinnedMeshRenderer* SkinnedMeshRendererComponent{ nullptr };
        std::uint32_t SkinArrayIndex{ 0 };
        const Game::RegisteredMaterialGroup* ResolvedMaterialGroup{ nullptr };
        std::uint32_t MaterialFlags{ 0 };
        std::uint32_t PickFlags{ 0 };
    };

    struct DrawRecordCandidate final {
        const Interface::IPipeline* Pso{ nullptr };
        const Game::ModelNode* Mesh{ nullptr };
        std::uint32_t SubMeshIndex{ 0 };
        std::uint32_t Pass{ 0 };
        std::uint32_t MaterialIndex{ 0 };
        std::uint32_t Flags{ 0 };
    };

    struct TransformWriteBack final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        SimpleMath::Matrix WorldMatrix{ SimpleMath::Matrix::Identity };
    };

    struct BoundingBoxWriteBack final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        bool ShouldSetObbAndWorldObb{ false };
        SimpleMath::Matrix WorldMatrix{ SimpleMath::Matrix::Identity };
        DirectX::BoundingOrientedBox Obb{ DirectX::BoundingOrientedBox{} };
        DirectX::BoundingOrientedBox WorldObb{ DirectX::BoundingOrientedBox{} };
    };

    struct ModelContextCandidate final {
        SimpleMath::Matrix World{ SimpleMath::Matrix::Identity };
        std::uint32_t Flags{ 0 };
    };

    struct JobOutput final {
        std::unordered_map<Arche::EntityID, SimpleMath::Matrix> JobLocalWorldMatrices{};
        std::vector<SimpleMath::Matrix> LocalBonePalette{};
        std::optional<ModelContextCandidate> LocalModelContextCandidate{};
        std::vector<DrawRecordCandidate> LocalDrawRecordCandidates{};
        std::vector<Game::RFD::BoundingBoxContext> LocalBoundingBoxContextCandidates{};
        std::vector<TransformWriteBack> TransformWriteBacks{};
        std::vector<BoundingBoxWriteBack> BoundingBoxWriteBacks{};
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

    bool TryResolveWorldMatrix(Arche::World& World, Arche::EntityID EntityId, const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& GlobalWorldMatrices, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutJobLocalWorldMatrices, SimpleMath::Matrix& OutWorldMatrix) {
        const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CachedLocalWorldMatrixIter{ InOutJobLocalWorldMatrices.find(EntityId) };
        if (CachedLocalWorldMatrixIter != InOutJobLocalWorldMatrices.end()) {
            OutWorldMatrix = CachedLocalWorldMatrixIter->second;
            return true;
        }

        const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CachedGlobalWorldMatrixIter{ GlobalWorldMatrices.find(EntityId) };
        if (CachedGlobalWorldMatrixIter != GlobalWorldMatrices.end()) {
            OutWorldMatrix = CachedGlobalWorldMatrixIter->second;
            return true;
        }

        std::vector<Arche::EntityID> EntityPath{};
        Arche::EntityID CurrentEntityId{ EntityId };
        SimpleMath::Matrix ParentWorldMatrix{ SimpleMath::Matrix::Identity };

        while (CurrentEntityId != Arche::NullEntityID) {
            const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CurrentCachedLocalWorldMatrixIter{ InOutJobLocalWorldMatrices.find(CurrentEntityId) };
            if (CurrentCachedLocalWorldMatrixIter != InOutJobLocalWorldMatrices.end()) {
                ParentWorldMatrix = CurrentCachedLocalWorldMatrixIter->second;
                break;
            }

            const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CurrentCachedGlobalWorldMatrixIter{ GlobalWorldMatrices.find(CurrentEntityId) };
            if (CurrentCachedGlobalWorldMatrixIter != GlobalWorldMatrices.end()) {
                ParentWorldMatrix = CurrentCachedGlobalWorldMatrixIter->second;
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
            InOutJobLocalWorldMatrices[CurrentPathEntityId] = CurrentWorldMatrix;
            ParentWorldMatrix = CurrentWorldMatrix;
        }

        OutWorldMatrix = ParentWorldMatrix;
        return true;
    }

    void AppendBoundingBoxContextFromWorldObb(const DirectX::BoundingOrientedBox& WorldObb, std::vector<Game::RFD::BoundingBoxContext>& InOutBoundingBoxContextCandidates) {
        Game::RFD::BoundingBoxContext BoundingBoxContext{};
        BoundingBoxContext.center = SimpleMath::Vector4{ WorldObb.Center.x, WorldObb.Center.y, WorldObb.Center.z, 1.0f };
        BoundingBoxContext.extents = SimpleMath::Vector4{ WorldObb.Extents.x, WorldObb.Extents.y, WorldObb.Extents.z, 0.0f };
        BoundingBoxContext.orientation = SimpleMath::Vector4{ WorldObb.Orientation.x, WorldObb.Orientation.y, WorldObb.Orientation.z, WorldObb.Orientation.w };
        InOutBoundingBoxContextCandidates.push_back(BoundingBoxContext);
    }

    void GatherBonePoseAndBoundingRecursive(Arche::World& World, Arche::EntityID EntityId, const JobInput& Input, const SimpleMath::Matrix& MeshWorldInverseMatrix, const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& GlobalWorldMatrices, JobOutput& InOutOutput, std::vector<SimpleMath::Matrix>* InOutBoneMatrices, std::optional<DirectX::BoundingBox>& InOutMergedAabb) {
        if (EntityId == Arche::NullEntityID || Input.SkinnedMeshRendererComponent == nullptr || Input.SkinnedMeshRendererComponent->model == nullptr) {
            return;
        }

        const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(EntityId) };
        if (HierarchyComponent == nullptr) {
            return;
        }

        const Game::Bone* BoneComponent{ World.GetComponent<Game::Bone>(EntityId) };
        if (BoneComponent != nullptr && BoneComponent->model == Input.SkinnedMeshRendererComponent->model) {
            SimpleMath::Matrix BoneWorldMatrix{};
            const bool IsBoneWorldMatrixResolved{ TryResolveWorldMatrix(World, EntityId, GlobalWorldMatrices, InOutOutput.JobLocalWorldMatrices, BoneWorldMatrix) };
            if (IsBoneWorldMatrixResolved == true) {
                const std::span<const Game::RuntimeBoneInfo> RuntimeBoneInfos{ Input.SkinnedMeshRendererComponent->model->GetRuntimeBoneInfos(BoneComponent->runtimeBoneInfoOffset, BoneComponent->runtimeBoneInfoCount) };
                for (const Game::RuntimeBoneInfo& RuntimeBoneInfoItem : RuntimeBoneInfos) {
                    if (RuntimeBoneInfoItem.SkinArrayIndex != Input.SkinArrayIndex) {
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
                    if (InOutMergedAabb.has_value() == true) {
                        DirectX::BoundingBox::CreateMerged(*InOutMergedAabb, *InOutMergedAabb, BoneWorldAabb);
                    }
                    else {
                        InOutMergedAabb = BoneWorldAabb;
                    }

                    AppendBoundingBoxContextFromWorldObb(BoneWorldObb, InOutOutput.LocalBoundingBoxContextCandidates);

                    BoundingBoxWriteBack WriteBack{};
                    WriteBack.EntityId = EntityId;
                    WriteBack.ShouldSetObbAndWorldObb = false;
                    WriteBack.WorldMatrix = BoneWorldMatrix;
                    InOutOutput.BoundingBoxWriteBacks.push_back(WriteBack);
                }
            }
        }

        Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
        while (ChildEntityId != Arche::NullEntityID) {
            const Game::EntityHierarchy* ChildHierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(ChildEntityId) };
            if (ChildHierarchyComponent == nullptr) {
                break;
            }

            GatherBonePoseAndBoundingRecursive(World, ChildEntityId, Input, MeshWorldInverseMatrix, GlobalWorldMatrices, InOutOutput, InOutBoneMatrices, InOutMergedAabb);
            ChildEntityId = ChildHierarchyComponent->nextSibling;
        }
    }

    void BuildJobInput(Arche::World& World, const Game::FrameContext& Ctx, const std::vector<Game::RegisteredMaterialGroup>& MaterialGroups, const Game::SkinnedMeshRenderer& SkinnedMeshRendererComponent, const Game::EntityHierarchy& EntityHierarchyComponent, std::vector<JobInput>& InOutInputs) {
        if (SkinnedMeshRendererComponent.active == false || SkinnedMeshRendererComponent.model == nullptr) {
            return;
        }

        const Arche::EntityID EntityId{ EntityHierarchyComponent.self };
        const ResolvedAnimator ResolvedAnimatorComponent{ ResolveAnimatorInHierarchy(World, EntityId) };
        if (ResolvedAnimatorComponent.Component == nullptr) {
            return;
        }

        const ResolvedBoneSkinReference ResolvedBoneSkinReferenceComponent{ ResolveBoneSkinReferenceInHierarchy(World, EntityId) };
        if (ResolvedBoneSkinReferenceComponent.Component == nullptr) {
            return;
        }

        const Arche::EntityID BoneRootEntityId{ ResolvedBoneSkinReferenceComponent.Component->boneRootEntityId };
        if (BoneRootEntityId == Arche::NullEntityID) {
            return;
        }

        const std::vector<Game::ModelNode>& Nodes{ SkinnedMeshRendererComponent.model->GetNodes() };
        if (SkinnedMeshRendererComponent.nodeIndex >= Nodes.size()) {
            return;
        }

        const Game::ModelNode& Node{ Nodes[SkinnedMeshRendererComponent.nodeIndex] };
        const std::uint32_t SkinArrayIndex{ Node.GetId() };

        const Game::Material* MaterialComponent{ World.GetComponent<Game::Material>(EntityId) };
        const std::uint32_t MaterialFlags{ MaterialComponent == nullptr ? 0u : MaterialComponent->Flags };
        const bool IsPickedHierarchy{ IsEntityWithinPickedHierarchy(World, EntityId, Ctx.PickedEntityId) };
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

        JobInput Input{};
        Input.EntityId = EntityId;
        Input.BoneRootEntityId = BoneRootEntityId;
        Input.SkinnedMeshRendererComponent = &SkinnedMeshRendererComponent;
        Input.SkinArrayIndex = SkinArrayIndex;
        Input.ResolvedMaterialGroup = ResolvedMaterialGroup;
        Input.MaterialFlags = MaterialFlags;
        Input.PickFlags = PickFlags;
        InOutInputs.push_back(Input);
    }

    JobOutput ExecuteJob(Arche::World& World, const Game::FrameContext& Ctx, const JobInput& Input) {
        JobOutput Output{};

        SimpleMath::Matrix MeshWorldMatrix{};
        const bool IsMeshWorldMatrixResolved{ TryResolveWorldMatrix(World, Input.EntityId, Ctx.WorldMatrices, Output.JobLocalWorldMatrices, MeshWorldMatrix) };
        if (IsMeshWorldMatrixResolved == false) {
            return Output;
        }

        Output.TransformWriteBacks.push_back(TransformWriteBack{ Input.EntityId, MeshWorldMatrix });

        BoundingBoxWriteBack MeshBoundingWorldWriteBack{};
        MeshBoundingWorldWriteBack.EntityId = Input.EntityId;
        MeshBoundingWorldWriteBack.ShouldSetObbAndWorldObb = false;
        MeshBoundingWorldWriteBack.WorldMatrix = MeshWorldMatrix;
        Output.BoundingBoxWriteBacks.push_back(MeshBoundingWorldWriteBack);

        const std::vector<Game::ModelNode>& Nodes{ Input.SkinnedMeshRendererComponent->model->GetNodes() };
        const Game::ModelNode& Node{ Nodes[Input.SkinnedMeshRendererComponent->nodeIndex] };

        std::vector<SimpleMath::Matrix> BoneMatrices{};
        std::span<const SimpleMath::Matrix> ResolvedBoneMatrices{};
        bool IsCacheHit{ false };
        const std::unordered_map<Arche::EntityID, Game::SkinnedPoseCacheEntry>::const_iterator CacheIter{ Ctx.SkinnedPoseCache.find(Input.EntityId) };
        if (CacheIter != Ctx.SkinnedPoseCache.end()) {
            const Game::SkinnedPoseCacheEntry& CacheEntry{ CacheIter->second };
            if (CacheEntry.IsValid == true && CacheEntry.SkinArrayIndex == Input.SkinArrayIndex && CacheEntry.BoneMatrices.empty() == false) {
                ResolvedBoneMatrices = CacheEntry.BoneMatrices;
                IsCacheHit = true;
            }
        }

        std::uint32_t BoneMatrixCount{ 0 };
        const bool IsBoneMatrixCountResolved{ Input.SkinnedMeshRendererComponent->model->TryGetRuntimeBoneMatrixCount(Input.SkinArrayIndex, BoneMatrixCount) };
        if (IsBoneMatrixCountResolved == false || BoneMatrixCount == 0) {
            return Output;
        }

        if (IsCacheHit == false) {
            BoneMatrices.resize(static_cast<std::size_t>(BoneMatrixCount), SimpleMath::Matrix::Identity);
        }

        std::optional<DirectX::BoundingBox> MergedBoneAabb{};
        SimpleMath::Matrix MeshWorldInverseMatrix{ MeshWorldMatrix };
        MeshWorldInverseMatrix = MeshWorldInverseMatrix.Invert();
        GatherBonePoseAndBoundingRecursive(World, Input.BoneRootEntityId, Input, MeshWorldInverseMatrix, Ctx.WorldMatrices, Output, IsCacheHit == false ? &BoneMatrices : nullptr, MergedBoneAabb);

        const Game::BoundingBox* BoundingBoxComponent{ World.GetComponent<Game::BoundingBox>(Input.EntityId) };
        DirectX::BoundingOrientedBox ResolvedMeshWorldObb{};
        if (BoundingBoxComponent != nullptr) {
            BoundingBoxComponent->GetObb().Transform(ResolvedMeshWorldObb, MeshWorldMatrix);

            if (MergedBoneAabb.has_value() == true) {
                DirectX::BoundingOrientedBox MergedBoneObb{};
                DirectX::BoundingOrientedBox::CreateFromBoundingBox(MergedBoneObb, *MergedBoneAabb);
                ResolvedMeshWorldObb = MergedBoneObb;
                BoundingBoxWriteBack MeshMergedBoundingWriteBack{};
                MeshMergedBoundingWriteBack.EntityId = Input.EntityId;
                MeshMergedBoundingWriteBack.ShouldSetObbAndWorldObb = true;
                MeshMergedBoundingWriteBack.Obb = MergedBoneObb;
                MeshMergedBoundingWriteBack.WorldObb = MergedBoneObb;
                Output.BoundingBoxWriteBacks.push_back(MeshMergedBoundingWriteBack);
            }

            AppendBoundingBoxContextFromWorldObb(ResolvedMeshWorldObb, Output.LocalBoundingBoxContextCandidates);
        }

        if (IsCacheHit == false) {
            ResolvedBoneMatrices = BoneMatrices;
        }

        const std::vector<Game::ModelSubMesh>& SubMeshes{ Node.GetSubMeshes() };
        if (SubMeshes.empty()) {
            return Output;
        }

        if (ResolvedBoneMatrices.empty() == true) {
            return Output;
        }

        Output.LocalBonePalette.insert(Output.LocalBonePalette.end(), ResolvedBoneMatrices.begin(), ResolvedBoneMatrices.end());

        ModelContextCandidate ModelContext{};
        ModelContext.World = MeshWorldMatrix;
        ModelContext.Flags = SkinnedModelContextFlagBitMask;
        Output.LocalModelContextCandidate = ModelContext;

        for (std::size_t SubMeshIndex{ 0 }; SubMeshIndex < SubMeshes.size(); ++SubMeshIndex) {
            const Game::ModelSubMesh& SubMesh{ SubMeshes[SubMeshIndex] };
            const Interface::IPipeline* Pipeline{ nullptr };
            std::uint32_t ResolvedMaterialIndex{ 0 };

            if (Input.ResolvedMaterialGroup != nullptr) {
                std::size_t ResolvedItemIndex{ SubMesh.MaterialGroupItemIndex };
                if (ResolvedItemIndex >= Input.ResolvedMaterialGroup->Items.size()) {
                    ResolvedItemIndex = 0;
                }

                if (ResolvedItemIndex < Input.ResolvedMaterialGroup->Items.size()) {
                    const Game::RegisteredMaterialGroupItem& RegisteredGroupItem{ Input.ResolvedMaterialGroup->Items[ResolvedItemIndex] };
                    Pipeline = RegisteredGroupItem.Pipeline;
                    ResolvedMaterialIndex = RegisteredGroupItem.MaterialIndex;
                }
            }

            DrawRecordCandidate DrawRecord{};
            DrawRecord.Pso = Pipeline;
            DrawRecord.Mesh = &Node;
            DrawRecord.SubMeshIndex = static_cast<std::uint32_t>(SubMeshIndex);
            DrawRecord.Pass = 0;
            DrawRecord.MaterialIndex = ResolvedMaterialIndex;
            DrawRecord.Flags = Input.MaterialFlags | Input.PickFlags;
            Output.LocalDrawRecordCandidates.push_back(DrawRecord);
        }

        return Output;
    }

    void MergeJobLocalWorldMatrices(const std::vector<JobOutput>& JobOutputs, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        for (const JobOutput& Output : JobOutputs) {
            for (const auto& [EntityId, WorldMatrix] : Output.JobLocalWorldMatrices) {
                InOutWorldMatrices.try_emplace(EntityId, WorldMatrix);
            }
        }
    }

    void ApplyWriteBacks(Arche::World& World, const std::vector<JobOutput>& JobOutputs) {
        for (const JobOutput& Output : JobOutputs) {
            for (const TransformWriteBack& TransformWrite : Output.TransformWriteBacks) {
                Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(TransformWrite.EntityId) };
                if (TransformComponent != nullptr) {
                    TransformComponent->worldMatrix = TransformWrite.WorldMatrix;
                }
            }
        }

        for (const JobOutput& Output : JobOutputs) {
            for (const BoundingBoxWriteBack& BoundingBoxWrite : Output.BoundingBoxWriteBacks) {
                Game::BoundingBox* BoundingBoxComponent{ World.GetComponent<Game::BoundingBox>(BoundingBoxWrite.EntityId) };
                if (BoundingBoxComponent == nullptr) {
                    continue;
                }

                if (BoundingBoxWrite.ShouldSetObbAndWorldObb == true) {
                    BoundingBoxComponent->SetObb(BoundingBoxWrite.Obb);
                    BoundingBoxComponent->SetWorldObb(BoundingBoxWrite.WorldObb);
                }
                else {
                    BoundingBoxComponent->UpdateWorldObb(BoundingBoxWrite.WorldMatrix);
                }
            }
        }
    }

    void MergeRenderOutputs(const std::vector<JobOutput>& JobOutputs, Game::RFD::RenderFrameData& InOutRenderData) {
        for (const JobOutput& Output : JobOutputs) {
            InOutRenderData.boundingBoxContexts.insert(InOutRenderData.boundingBoxContexts.end(), Output.LocalBoundingBoxContextCandidates.begin(), Output.LocalBoundingBoxContextCandidates.end());

            if (Output.LocalModelContextCandidate.has_value() == false || Output.LocalBonePalette.empty()) {
                continue;
            }

            const std::uint32_t BoneIndexStart{ static_cast<std::uint32_t>(InOutRenderData.bonePalette.size()) };
            InOutRenderData.bonePalette.insert(InOutRenderData.bonePalette.end(), Output.LocalBonePalette.begin(), Output.LocalBonePalette.end());

            Game::RFD::ModelContext ModelContext{};
            ModelContext.world = Output.LocalModelContextCandidate->World;
            ModelContext.prevWorld = ModelContext.world;
            ModelContext.flags = Output.LocalModelContextCandidate->Flags;
            ModelContext.boneIndexStart = BoneIndexStart;
            ModelContext.objectID = static_cast<std::uint32_t>(InOutRenderData.modelContexts.size());
            InOutRenderData.modelContexts.push_back(ModelContext);

            for (const DrawRecordCandidate& DrawRecordCandidateItem : Output.LocalDrawRecordCandidates) {
                Game::RFD::DrawRecord DrawRecord{};
                DrawRecord.pso = DrawRecordCandidateItem.Pso;
                DrawRecord.mesh = DrawRecordCandidateItem.Mesh;
                DrawRecord.submesh = DrawRecordCandidateItem.SubMeshIndex;
                DrawRecord.pass = DrawRecordCandidateItem.Pass;
                DrawRecord.objectIndex = ModelContext.objectID;
                DrawRecord.materialIndex = DrawRecordCandidateItem.MaterialIndex;
                DrawRecord.flags = DrawRecordCandidateItem.Flags;
                InOutRenderData.drawRecords.push_back(DrawRecord);
            }
        }
    }

}

namespace Game {
    class SkinnedMeshRenderSystemWorkerState final {
    public:
        SkinnedMeshRenderSystemWorkerState();
        ~SkinnedMeshRenderSystemWorkerState();
        SkinnedMeshRenderSystemWorkerState(const SkinnedMeshRenderSystemWorkerState& Other) = delete;
        SkinnedMeshRenderSystemWorkerState& operator=(const SkinnedMeshRenderSystemWorkerState& Other) = delete;
        SkinnedMeshRenderSystemWorkerState(SkinnedMeshRenderSystemWorkerState&& Other) noexcept = delete;
        SkinnedMeshRenderSystemWorkerState& operator=(SkinnedMeshRenderSystemWorkerState&& Other) noexcept = delete;

    public:
        std::vector<JobOutput> ExecuteJobs(Arche::World& World, const FrameContext& Ctx, const std::vector<JobInput>& JobInputs);

    private:
        void WorkerLoop();

    private:
        std::vector<std::thread> mWorkerThreads{};
        std::mutex mMutex{};
        std::condition_variable mJobConditionVariable{};
        std::condition_variable mDoneConditionVariable{};
        std::atomic<std::size_t> mNextJobIndex{ 0 };
        std::atomic<std::size_t> mCompletedJobCount{ 0 };
        Arche::World* mWorld{ nullptr };
        const FrameContext* mFrameContext{ nullptr };
        const std::vector<JobInput>* mJobInputs{ nullptr };
        std::vector<JobOutput>* mJobOutputs{ nullptr };
        std::size_t mBatchJobCount{ 0 };
        std::uint64_t mBatchId{ 0 };
        bool mShouldStop{ false };
    };

    SkinnedMeshRenderSystemWorkerState::SkinnedMeshRenderSystemWorkerState() {
        std::uint32_t HardwareThreadCount{ std::thread::hardware_concurrency() };
        if (HardwareThreadCount == 0) {
            HardwareThreadCount = 2;
        }

        std::uint32_t WorkerThreadCount{ HardwareThreadCount / 2 };
        if (WorkerThreadCount == 0) {
            WorkerThreadCount = 1;
        }

        mWorkerThreads.reserve(WorkerThreadCount);
        for (std::uint32_t WorkerIndex{ 0 }; WorkerIndex < WorkerThreadCount; ++WorkerIndex) {
            mWorkerThreads.emplace_back(&SkinnedMeshRenderSystemWorkerState::WorkerLoop, this);
        }
    }

    SkinnedMeshRenderSystemWorkerState::~SkinnedMeshRenderSystemWorkerState() {
        {
            std::lock_guard<std::mutex> LockGuard{ mMutex };
            mShouldStop = true;
            ++mBatchId;
        }

        mJobConditionVariable.notify_all();

        for (std::thread& WorkerThread : mWorkerThreads) {
            if (WorkerThread.joinable() == true) {
                WorkerThread.join();
            }
        }
    }

    std::vector<JobOutput> SkinnedMeshRenderSystemWorkerState::ExecuteJobs(Arche::World& World, const FrameContext& Ctx, const std::vector<JobInput>& JobInputs) {
        std::vector<JobOutput> JobOutputs{};
        JobOutputs.resize(JobInputs.size());
        if (JobInputs.empty() == true) {
            return JobOutputs;
        }

        {
            std::lock_guard<std::mutex> LockGuard{ mMutex };
            mWorld = &World;
            mFrameContext = &Ctx;
            mJobInputs = &JobInputs;
            mJobOutputs = &JobOutputs;
            mBatchJobCount = JobInputs.size();
            mNextJobIndex.store(0);
            mCompletedJobCount.store(0);
            ++mBatchId;
        }

        mJobConditionVariable.notify_all();

        std::unique_lock<std::mutex> UniqueLock{ mMutex };
        mDoneConditionVariable.wait(
            UniqueLock,
            [this]() {
                return mCompletedJobCount.load() >= mBatchJobCount;
            });

        mWorld = nullptr;
        mFrameContext = nullptr;
        mJobInputs = nullptr;
        mJobOutputs = nullptr;
        mBatchJobCount = 0;

        return JobOutputs;
    }

    void SkinnedMeshRenderSystemWorkerState::WorkerLoop() {
        std::uint64_t LastObservedBatchId{ 0 };
        while (true) {
            Arche::World* World{ nullptr };
            const FrameContext* Ctx{ nullptr };
            const std::vector<JobInput>* JobInputs{ nullptr };
            std::vector<JobOutput>* JobOutputs{ nullptr };
            std::size_t JobCount{ 0 };

            {
                std::unique_lock<std::mutex> UniqueLock{ mMutex };
                mJobConditionVariable.wait(
                    UniqueLock,
                    [this, LastObservedBatchId]() {
                        return mShouldStop == true || mBatchId != LastObservedBatchId;
                    });

                if (mShouldStop == true) {
                    return;
                }

                LastObservedBatchId = mBatchId;
                World = mWorld;
                Ctx = mFrameContext;
                JobInputs = mJobInputs;
                JobOutputs = mJobOutputs;
                JobCount = mBatchJobCount;
            }

            if (World == nullptr || Ctx == nullptr || JobInputs == nullptr || JobOutputs == nullptr) {
                continue;
            }

            while (true) {
                const std::size_t JobIndex{ mNextJobIndex.fetch_add(1) };
                if (JobIndex >= JobCount) {
                    break;
                }

                (*JobOutputs)[JobIndex] = ExecuteJob(*World, *Ctx, (*JobInputs)[JobIndex]);
                const std::size_t CompletedJobCount{ mCompletedJobCount.fetch_add(1) + 1 };
                if (CompletedJobCount == JobCount) {
                    std::lock_guard<std::mutex> LockGuard{ mMutex };
                    mDoneConditionVariable.notify_one();
                }
            }
        }
    }

    SkinnedMeshRenderSystem::SkinnedMeshRenderSystem()
        : mWorkerState{ std::make_unique<SkinnedMeshRenderSystemWorkerState>() } {
    }

    SkinnedMeshRenderSystem::~SkinnedMeshRenderSystem() = default;

    const std::string& SkinnedMeshRenderSystem::Name() const {
        return mName;
    }

    Phase SkinnedMeshRenderSystem::GetPhase() const {
        return Phase::Render;
    }

    std::span<const ComponentAccess> SkinnedMeshRenderSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 6> Accesses{ { { typeid(Animator), Access::Read }, { typeid(BoneSkinReference), Access::Read }, { typeid(SkinnedMeshRenderer), Access::Read }, { typeid(Transform), Access::Write }, { typeid(EntityHierarchy), Access::Read }, { typeid(BoundingBox), Access::Write } } };
        return Accesses;
    }

    std::span<const ResourceAccess> SkinnedMeshRenderSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 5> Accesses{ { { typeid(RFD::RenderFrameData), Access::Write }, { typeid(std::vector<RegisteredMaterialGroup>), Access::Read }, { typeid(Arche::EntityID), Access::Read }, { typeid(std::unordered_map<Arche::EntityID, SimpleMath::Matrix>), Access::Read }, { typeid(std::unordered_map<Arche::EntityID, SkinnedPoseCacheEntry>), Access::Read } } };
        return Accesses;
    }

    void SkinnedMeshRenderSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        RFD::RenderFrameData& RenderData{ Ctx.RenderData };
        const std::vector<RegisteredMaterialGroup>& MaterialGroups{ *Ctx.MaterialGroups };

        std::vector<JobInput> JobInputs{};
        for (auto [SkinnedMeshRendererComponent, TransformComponent, EntityHierarchyComponent] : World.Query<SkinnedMeshRenderer, Transform, EntityHierarchy>()) {
            (void)TransformComponent;
            BuildJobInput(World, Ctx, MaterialGroups, SkinnedMeshRendererComponent, EntityHierarchyComponent, JobInputs);
        }

        std::vector<JobOutput> JobOutputs{ mWorkerState->ExecuteJobs(World, Ctx, JobInputs) };

        MergeJobLocalWorldMatrices(JobOutputs, Ctx.WorldMatrices);
        ApplyWriteBacks(World, JobOutputs);
        MergeRenderOutputs(JobOutputs, RenderData);
    }
}
