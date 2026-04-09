#include "SkinnedMeshRenderSystem.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "Game/Base/Common.h"
#include "Game/Model/AssetRegistry.h"
#include "Game/Model/Model.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/BoneSkinReference.h"
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

    void GatherBonePoseAndBoundingRecursive(Arche::World& World, Arche::EntityID EntityId, Game::Model* ModelData, std::uint32_t SkinArrayIndex, const SimpleMath::Matrix& MeshWorldInverseMatrix, std::vector<SimpleMath::Matrix>* InOutBoneMatrices, std::optional<DirectX::BoundingBox>* InOutMergedAabb, Game::RFD::RenderFrameData* InOutRenderData) {
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

                Game::BoundingBox* BoundingBoxComponent{ World.GetComponent<Game::BoundingBox>(EntityId) };
                if (BoundingBoxComponent != nullptr) {
                    BoundingBoxComponent->UpdateWorldObb(BoneWorldMatrix);
                    if (BoundingBoxComponent->HasWorldObb() == true) {
                        const DirectX::BoundingOrientedBox& BoneWorldObb{ BoundingBoxComponent->GetWorldObb() };
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

                        if (InOutRenderData != nullptr) {
                            Game::RFD::BoundingBoxContext BoneBoundingBoxContext{};
       
                            BoneBoundingBoxContext.center = SimpleMath::Vector4{ BoneWorldObb.Center.x, BoneWorldObb.Center.y, BoneWorldObb.Center.z, 1.0f };
                            BoneBoundingBoxContext.extents = SimpleMath::Vector4{ BoneWorldObb.Extents.x, BoneWorldObb.Extents.y, BoneWorldObb.Extents.z, 0.0f };
                            BoneBoundingBoxContext.orientation = SimpleMath::Vector4{ BoneWorldObb.Orientation.x, BoneWorldObb.Orientation.y, BoneWorldObb.Orientation.z, BoneWorldObb.Orientation.w };


                            InOutRenderData->boundingBoxContexts.push_back(BoneBoundingBoxContext);
                        }
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

            GatherBonePoseAndBoundingRecursive(World, ChildEntityId, ModelData, SkinArrayIndex, MeshWorldInverseMatrix, InOutBoneMatrices, InOutMergedAabb, InOutRenderData);
            ChildEntityId = ChildHierarchyComponent->nextSibling;
        }
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

            SimpleMath::Matrix MeshWorldMatrix{};
            const bool IsMeshWorldMatrixResolved{ TryResolveWorldMatrix(World, EntityId, MeshWorldMatrix) };
            if (IsMeshWorldMatrixResolved == false) {
                continue;
            }

            BoundingBox* BoundingBoxComponent{ World.GetComponent<BoundingBox>(EntityId) };

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

            std::vector<SimpleMath::Matrix> BoneMatrices{};
            BoneMatrices.resize(static_cast<std::size_t>(BoneMatrixCount), SimpleMath::Matrix::Identity);
            std::span<const SimpleMath::Matrix> ResolvedBoneMatrices{};

            std::optional<DirectX::BoundingBox> MergedBoneAabb{};
            SimpleMath::Matrix MeshWorldInverseMatrix{ MeshWorldMatrix };
            MeshWorldInverseMatrix = MeshWorldInverseMatrix.Invert();
            GatherBonePoseAndBoundingRecursive(World, BoneRootEntityId, SkinnedMeshRendererComponent.model, SkinArrayIndex, MeshWorldInverseMatrix, &BoneMatrices, &MergedBoneAabb, &RenderData);
            if (BoundingBoxComponent != nullptr && MergedBoneAabb.has_value() == true) {
                DirectX::BoundingOrientedBox MergedBoneObb{};
                DirectX::BoundingOrientedBox::CreateFromBoundingBox(MergedBoneObb, *MergedBoneAabb);
                BoundingBoxComponent->SetObb(MergedBoneObb);
                BoundingBoxComponent->SetWorldObb(MergedBoneObb);
            }

            ResolvedBoneMatrices = BoneMatrices;

            const std::vector<ModelSubMesh>& SubMeshes{ Node.GetSubMeshes() };
            if (SubMeshes.empty()) {
                continue;
            }

            if (ResolvedBoneMatrices.empty() == true) {
                continue;
            }

            const std::uint32_t BoneIndexStart{ static_cast<std::uint32_t>(RenderData.bonePalette.size()) };
            RenderData.bonePalette.insert(RenderData.bonePalette.end(), ResolvedBoneMatrices.begin(), ResolvedBoneMatrices.end());

            RFD::ModelContext ModelContext{};
            ModelContext.world = MeshWorldMatrix;
            ModelContext.prevWorld = ModelContext.world;

            if (BoundingBoxComponent != nullptr && BoundingBoxComponent->HasWorldObb() == true) {
                const DirectX::BoundingOrientedBox& WorldObb{ BoundingBoxComponent->GetWorldObb() };
                RFD::BoundingBoxContext BoundingBoxContext{};

                BoundingBoxContext.center = SimpleMath::Vector4{ WorldObb.Center.x, WorldObb.Center.y, WorldObb.Center.z, 1.0f };
                BoundingBoxContext.extents = SimpleMath::Vector4{ WorldObb.Extents.x, WorldObb.Extents.y, WorldObb.Extents.z, 0.0f };
                BoundingBoxContext.orientation = SimpleMath::Vector4{ WorldObb.Orientation.x, WorldObb.Orientation.y, WorldObb.Orientation.z, WorldObb.Orientation.w };

                RenderData.boundingBoxContexts.push_back(BoundingBoxContext);
            }

            ModelContext.flags = SkinnedModelContextFlagBitMask;
            ModelContext.boneIndexStart = BoneIndexStart;
            ModelContext.objectID = static_cast<std::uint32_t>(RenderData.modelContexts.size());
            RenderData.modelContexts.push_back(ModelContext);

            const Material* MaterialComponent{ World.GetComponent<Material>(EntityId) };
            const std::uint32_t MaterialFlags{ MaterialComponent == nullptr ? 0u : MaterialComponent->Flags };
            const bool IsPickedHierarchy{ IsEntityWithinPickedHierarchy(World, EntityId, Ctx.PickedEntityId) };
            const std::uint32_t PickFlags{ IsPickedHierarchy ? PickedDrawFlagBitMask : 0u };
            const RegisteredMaterialGroup* ResolvedMaterialGroup{ nullptr };
            if (MaterialGroups.empty() == false) {
                std::size_t ResolvedMaterialGroupIndex{ MaterialComponent == nullptr ? 0u : MaterialComponent->MaterialGroupIndex };
                if (ResolvedMaterialGroupIndex >= MaterialGroups.size() || MaterialGroups[ResolvedMaterialGroupIndex].Items.empty()) {
                    ResolvedMaterialGroupIndex = 0;
                }

                if (ResolvedMaterialGroupIndex < MaterialGroups.size() && MaterialGroups[ResolvedMaterialGroupIndex].Items.empty() == false) {
                    ResolvedMaterialGroup = &MaterialGroups[ResolvedMaterialGroupIndex];
                }
            }

            for (std::size_t SubMeshIndex{ 0 }; SubMeshIndex < SubMeshes.size(); ++SubMeshIndex) {
                const ModelSubMesh& SubMesh{ SubMeshes[SubMeshIndex] };
                const Interface::IPipeline* Pipeline{ nullptr };
                std::uint32_t ResolvedMaterialIndex{ 0 };

                if (ResolvedMaterialGroup != nullptr) {
                    std::size_t ResolvedItemIndex{ SubMesh.MaterialGroupItemIndex };
                    if (ResolvedItemIndex >= ResolvedMaterialGroup->Items.size()) {
                        ResolvedItemIndex = 0;
                    }

                    if (ResolvedItemIndex < ResolvedMaterialGroup->Items.size()) {
                        const RegisteredMaterialGroupItem& RegisteredGroupItem{ ResolvedMaterialGroup->Items[ResolvedItemIndex] };
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
                DrawRecord.flags = MaterialFlags | PickFlags;
                RenderData.drawRecords.push_back(DrawRecord);
            }
        }
    }
}
