#include "SkinnedRenderSystem.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include "Game/Model/AssetRegistry.h"
#include "Game/Model/Model.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Culling.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/SkinnedMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Base/Context.h"

namespace Game {
    namespace Pipeline {
        namespace {
            constexpr std::uint32_t SkinnedModelContextFlagBitMask{ 0x1u };
            constexpr std::uint32_t PickedDrawFlagBitMask{ 0x1u };

            struct ResolvedAnimator final {
            public:
                Arche::EntityID mEntityId{ Arche::NullEntityID };
                const Animator* mComponent{};
            };

            struct ResolvedBoneSkinReference final {
            public:
                Arche::EntityID mEntityId{ Arche::NullEntityID };
                const BoneSkinReference* mComponent{};
            };

            struct PreparedSkinnedMeshData final {
            public:
                Arche::EntityID mEntityId{ Arche::NullEntityID };
                std::vector<SimpleMath::Matrix> mBonePalette{};
                std::vector<RenderContract::BoundingBoxContext> mBoneBoundingBoxContexts{};
            };

            ResolvedAnimator ResolveAnimatorInHierarchy(PipelineContext& Ctx, Arche::EntityID StartEntityId) {
                Arche::EntityID CurrentEntityId{ StartEntityId };
                while (CurrentEntityId != Arche::NullEntityID) {
                    const Animator* AnimatorComponent{ Ctx.ReadComponent<Animator>(CurrentEntityId) };
                    if (AnimatorComponent != nullptr) {
                        return ResolvedAnimator{ CurrentEntityId, AnimatorComponent };
                    }

                    const EntityHierarchy* HierarchyComponent{ Ctx.ReadComponent<EntityHierarchy>(CurrentEntityId) };
                    if (HierarchyComponent == nullptr) {
                        break;
                    }

                    CurrentEntityId = HierarchyComponent->parent;
                }

                return ResolvedAnimator{};
            }

            ResolvedBoneSkinReference ResolveBoneSkinReferenceInHierarchy(PipelineContext& Ctx, Arche::EntityID StartEntityId) {
                Arche::EntityID CurrentEntityId{ StartEntityId };
                while (CurrentEntityId != Arche::NullEntityID) {
                    const BoneSkinReference* BoneSkinReferenceComponent{ Ctx.ReadComponent<BoneSkinReference>(CurrentEntityId) };
                    if (BoneSkinReferenceComponent != nullptr) {
                        return ResolvedBoneSkinReference{ CurrentEntityId, BoneSkinReferenceComponent };
                    }

                    const EntityHierarchy* HierarchyComponent{ Ctx.ReadComponent<EntityHierarchy>(CurrentEntityId) };
                    if (HierarchyComponent == nullptr) {
                        break;
                    }

                    CurrentEntityId = HierarchyComponent->parent;
                }

                return ResolvedBoneSkinReference{};
            }

            bool TryResolveTransformWorldMatrix(PipelineContext& Ctx, Arche::EntityID EntityId, SimpleMath::Matrix& OutWorldMatrix) {
                const Transform* TransformComponent{ Ctx.ReadComponent<Transform>(EntityId) };
                if (TransformComponent == nullptr) {
                    return false;
                }

                OutWorldMatrix = TransformComponent->worldMatrix;
                return true;
            }

            void AppendBoundingBoxContext(const DirectX::BoundingOrientedBox& WorldObb, std::vector<RenderContract::BoundingBoxContext>& OutBoundingBoxContexts) {
                RenderContract::BoundingBoxContext BoundingBoxContext{};
                BoundingBoxContext.mCenter = SimpleMath::Vector4{ WorldObb.Center.x, WorldObb.Center.y, WorldObb.Center.z, 1.0f };
                BoundingBoxContext.mExtents = SimpleMath::Vector4{ WorldObb.Extents.x, WorldObb.Extents.y, WorldObb.Extents.z, 0.0f };
                BoundingBoxContext.mOrientation = SimpleMath::Vector4{ WorldObb.Orientation.x, WorldObb.Orientation.y, WorldObb.Orientation.z, WorldObb.Orientation.w };
                OutBoundingBoxContexts.push_back(BoundingBoxContext);
            }

            void GatherBonePoseAndBoundingRecursive(PipelineContext& Ctx, Arche::EntityID EntityId, Model* ModelData, std::uint32_t SkinArrayIndex, const SimpleMath::Matrix& MeshWorldInverseMatrix, bool IsDrawBoundingBoxesEnabled, std::vector<SimpleMath::Matrix>& InOutBoneMatrices, std::vector<RenderContract::BoundingBoxContext>& InOutBoundingBoxContexts) {
                if (EntityId == Arche::NullEntityID || ModelData == nullptr) {
                    return;
                }

                const EntityHierarchy* HierarchyComponent{ Ctx.ReadComponent<EntityHierarchy>(EntityId) };
                if (HierarchyComponent == nullptr) {
                    return;
                }

                const Bone* BoneComponent{ Ctx.ReadComponent<Bone>(EntityId) };
                if (BoneComponent != nullptr && BoneComponent->model == ModelData) {
                    SimpleMath::Matrix BoneWorldMatrix{};
                    const bool IsBoneWorldMatrixResolved{ TryResolveTransformWorldMatrix(Ctx, EntityId, BoneWorldMatrix) };
                    if (IsBoneWorldMatrixResolved == true) {
                        const std::span<const RuntimeBoneInfo> RuntimeBoneInfos{ ModelData->GetRuntimeBoneInfos(BoneComponent->runtimeBoneInfoOffset, BoneComponent->runtimeBoneInfoCount) };
                        for (const RuntimeBoneInfo& RuntimeBoneInfoItem : RuntimeBoneInfos) {
                            if (RuntimeBoneInfoItem.mSkinArrayIndex != SkinArrayIndex) {
                                continue;
                            }

                            if (RuntimeBoneInfoItem.mJointArrayIndex < InOutBoneMatrices.size()) {
                                InOutBoneMatrices[RuntimeBoneInfoItem.mJointArrayIndex] = RuntimeBoneInfoItem.mInverseBindMatrix * BoneWorldMatrix * MeshWorldInverseMatrix;
                            }
                        }

                        BoundingBox* BoundingBoxComponent{ Ctx.WriteComponent<BoundingBox>(EntityId) };
                        if (BoundingBoxComponent != nullptr && IsDrawBoundingBoxesEnabled == true) {
                            DirectX::BoundingOrientedBox BoneWorldObb{};
                            BoundingBoxComponent->GetObb().Transform(BoneWorldObb, BoneWorldMatrix);
                            AppendBoundingBoxContext(BoneWorldObb, InOutBoundingBoxContexts);
                            BoundingBoxComponent->SetWorldObb(BoneWorldObb);
                        }
                    }
                }

                Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
                while (ChildEntityId != Arche::NullEntityID) {
                    const EntityHierarchy* ChildHierarchyComponent{ Ctx.ReadComponent<EntityHierarchy>(ChildEntityId) };
                    if (ChildHierarchyComponent == nullptr) {
                        break;
                    }

                    GatherBonePoseAndBoundingRecursive(Ctx, ChildEntityId, ModelData, SkinArrayIndex, MeshWorldInverseMatrix, IsDrawBoundingBoxesEnabled, InOutBoneMatrices, InOutBoundingBoxContexts);
                    ChildEntityId = ChildHierarchyComponent->nextSibling;
                }
            }

            bool TryBuildPreparedResult(PipelineContext& Ctx, Arche::EntityID EntityId, bool IsDrawBoundingBoxesEnabled, PreparedSkinnedMeshData& OutPreparedData) {
                const SkinnedMeshRenderer* Renderer{ Ctx.ReadComponent<SkinnedMeshRenderer>(EntityId) };
                if (Renderer == nullptr || Renderer->active == false || Renderer->model == nullptr) {
                    return false;
                }

                const ResolvedAnimator ResolvedAnimatorComponent{ ResolveAnimatorInHierarchy(Ctx, EntityId) };
                if (ResolvedAnimatorComponent.mComponent == nullptr) {
                    return false;
                }

                const ResolvedBoneSkinReference ResolvedBoneSkinReferenceComponent{ ResolveBoneSkinReferenceInHierarchy(Ctx, EntityId) };
                if (ResolvedBoneSkinReferenceComponent.mComponent == nullptr) {
                    return false;
                }

                const Arche::EntityID BoneRootEntityId{ ResolvedBoneSkinReferenceComponent.mComponent->boneRootEntityId };
                if (BoneRootEntityId == Arche::NullEntityID) {
                    return false;
                }

                SimpleMath::Matrix MeshWorldMatrix{};
                const bool IsMeshWorldMatrixResolved{ TryResolveTransformWorldMatrix(Ctx, EntityId, MeshWorldMatrix) };
                if (IsMeshWorldMatrixResolved == false) {
                    return false;
                }

                const std::vector<ModelNode>& Nodes{ Renderer->model->GetNodes() };
                if (Renderer->nodeIndex >= Nodes.size()) {
                    return false;
                }

                const ModelNode& Node{ Nodes[Renderer->nodeIndex] };
                const std::uint32_t SkinArrayIndex{ Node.GetId() };
                std::uint32_t BoneMatrixCount{};
                const bool IsBoneMatrixCountResolved{ Renderer->model->TryGetRuntimeBoneMatrixCount(SkinArrayIndex, BoneMatrixCount) };
                if (IsBoneMatrixCountResolved == false || BoneMatrixCount == 0u) {
                    return false;
                }

                OutPreparedData.mEntityId = EntityId;
                OutPreparedData.mBonePalette.resize(static_cast<std::size_t>(BoneMatrixCount), SimpleMath::Matrix::Identity);
                SimpleMath::Matrix MeshWorldInverseMatrix{ MeshWorldMatrix };
                MeshWorldInverseMatrix = MeshWorldInverseMatrix.Invert();
                GatherBonePoseAndBoundingRecursive(Ctx, BoneRootEntityId, Renderer->model, SkinArrayIndex, MeshWorldInverseMatrix, IsDrawBoundingBoxesEnabled, OutPreparedData.mBonePalette, OutPreparedData.mBoneBoundingBoxContexts);
                return true;
            }

            void UpdateAnimatorBoundingBoxes(PipelineContext& Ctx) {
                Ctx.ForEach<Animator, Transform, BoundingBox>([](Animator& AnimatorComponent, Transform& TransformComponent, BoundingBox& BoundingBoxComponent) {
                    (void)AnimatorComponent;

                    const DirectX::BoundingOrientedBox AnimatorLocalBoundingBox{ BoundingBoxComponent.GetObb() };
                    const SimpleMath::Matrix TransformOnlyWorldMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
                    DirectX::BoundingOrientedBox AnimatorWorldBoundingBox{};
                    AnimatorLocalBoundingBox.Transform(AnimatorWorldBoundingBox, TransformOnlyWorldMatrix);
                    BoundingBoxComponent.SetWorldObb(AnimatorWorldBoundingBox);
                });
            }

            bool IsEntityWithinPickedHierarchy(PipelineContext& Ctx, Arche::EntityID EntityId, Arche::EntityID PickedEntityId) {
                if (PickedEntityId == Arche::NullEntityID) {
                    return false;
                }

                Arche::EntityID CurrentEntityId{ EntityId };
                while (CurrentEntityId != Arche::NullEntityID) {
                    if (CurrentEntityId == PickedEntityId) {
                        return true;
                    }

                    const EntityHierarchy* HierarchyComponent{ Ctx.ReadComponent<EntityHierarchy>(CurrentEntityId) };
                    if (HierarchyComponent == nullptr) {
                        break;
                    }

                    CurrentEntityId = HierarchyComponent->parent;
                }

                return false;
            }

            const RegisteredMaterialGroup* ResolveMaterialGroup(const std::vector<RegisteredMaterialGroup>* MaterialGroups, const Material* MaterialComponent) {
                if (MaterialGroups == nullptr || MaterialGroups->empty() == true) {
                    return nullptr;
                }

                std::size_t ResolvedMaterialGroupIndex{ MaterialComponent == nullptr ? 0u : MaterialComponent->MaterialGroupIndex };
                if (ResolvedMaterialGroupIndex >= MaterialGroups->size() || (*MaterialGroups)[ResolvedMaterialGroupIndex].Items.empty() == true) {
                    ResolvedMaterialGroupIndex = 0u;
                }

                if (ResolvedMaterialGroupIndex >= MaterialGroups->size() || (*MaterialGroups)[ResolvedMaterialGroupIndex].Items.empty() == true) {
                    return nullptr;
                }

                return &(*MaterialGroups)[ResolvedMaterialGroupIndex];
            }

            bool IsVisibleByShadowBox(PipelineContext& Ctx, Arche::EntityID EntityId, const DirectX::BoundingOrientedBox& CullingBox) {
                const Culling* CullingComponent{ Ctx.ReadComponent<Culling>(EntityId) };
                if (CullingComponent != nullptr && CullingComponent->frustumCulling == false) {
                    return true;
                }

                const BoundingBox* BoundingBoxComponent{ Ctx.ReadComponent<BoundingBox>(EntityId) };
                if (BoundingBoxComponent == nullptr || BoundingBoxComponent->HasWorldObb() == false) {
                    return true;
                }

                return CullingBox.Intersects(BoundingBoxComponent->GetWorldObb());
            }

            void AppendSkinnedDrawRecords(const std::vector<ModelSubMesh>& SubMeshes, const ModelNode& Node, const RegisteredMaterialGroup* ResolvedMaterialGroup, std::uint32_t ObjectIndex, std::uint32_t MaterialFlags, std::uint32_t PickFlags, std::vector<RenderContract::DrawRecord>& OutDrawRecords) {
                for (std::size_t SubMeshIndex{}; SubMeshIndex < SubMeshes.size(); ++SubMeshIndex) {
                    const ModelSubMesh& SubMesh{ SubMeshes[SubMeshIndex] };
                    const RenderContract::IPipeline* Pipeline{};
                    std::uint32_t ResolvedMaterialIndex{};

                    if (ResolvedMaterialGroup != nullptr) {
                        std::size_t ResolvedItemIndex{ SubMesh.mMaterialGroupItemIndex };
                        if (ResolvedItemIndex >= ResolvedMaterialGroup->Items.size()) {
                            ResolvedItemIndex = 0u;
                        }

                        if (ResolvedItemIndex < ResolvedMaterialGroup->Items.size()) {
                            const RegisteredMaterialGroupItem& RegisteredGroupItem{ ResolvedMaterialGroup->Items[ResolvedItemIndex] };
                            Pipeline = RegisteredGroupItem.Pipeline;
                            ResolvedMaterialIndex = RegisteredGroupItem.MaterialIndex;
                        }
                    }

                    RenderContract::DrawRecord DrawRecord{};
                    DrawRecord.mPipeline = Pipeline;
                    DrawRecord.mMesh = &Node;
                    DrawRecord.mSubMesh = static_cast<std::uint32_t>(SubMeshIndex);
                    DrawRecord.mPass = 0u;
                    DrawRecord.mObjectIndex = ObjectIndex;
                    DrawRecord.mMaterialIndex = ResolvedMaterialIndex;
                    DrawRecord.mFlags = MaterialFlags | PickFlags;
                    DrawRecord.mPadding0 = 0u;
                    OutDrawRecords.push_back(DrawRecord);
                }
            }
        }

        PipelineSkinnedRenderSystem::PipelineSkinnedRenderSystem() {
        }

        PipelineSkinnedRenderSystem::~PipelineSkinnedRenderSystem() {
        }

        PipelineSkinnedRenderSystem::PipelineSkinnedRenderSystem(const PipelineSkinnedRenderSystem& Other) {
            (void)Other;
        }

        PipelineSkinnedRenderSystem& PipelineSkinnedRenderSystem::operator=(const PipelineSkinnedRenderSystem& Other) {
            (void)Other;
            return *this;
        }

        PipelineSkinnedRenderSystem::PipelineSkinnedRenderSystem(PipelineSkinnedRenderSystem&& Other) noexcept {
            (void)Other;
        }

        PipelineSkinnedRenderSystem& PipelineSkinnedRenderSystem::operator=(PipelineSkinnedRenderSystem&& Other) noexcept {
            (void)Other;
            return *this;
        }

        const std::string& PipelineSkinnedRenderSystem::Name() const {
            static const std::string NameText{ "SkinnedRenderSystem" };
            return NameText;
        }

        void PipelineSkinnedRenderSystem::Execute(PipelineContext& Ctx, float Dt) {
            (void)Dt;

            RenderContract::RenderGatherResult& GatherResult{ Ctx.GetRenderGatherResult() };
            const std::vector<RegisteredMaterialGroup>* MaterialGroups{ Ctx.GetMaterialGroups() };
            const RenderContract::ShadowMappingParameter& ShadowMappingParameter{ Ctx.GetShadowMappingParameter() };
            const std::uint32_t ShadowCascadeCount{ RenderContract::ResolveShadowCascadeCount(ShadowMappingParameter) };
            const std::array<DirectX::BoundingOrientedBox, RenderContract::ShadowCascadeMaxCount> ShadowCullingBoxes{ RenderContract::BuildShadowCullingBoxes(ShadowMappingParameter) };
            const bool IsDrawBoundingBoxesEnabled{ Ctx.HasRenderFlag(RenderContract::FrameGlobalFlagDrawBoundingBoxes) };
            std::unordered_set<Arche::EntityID> AppendedBoundingBoxEntities{};
            AppendedBoundingBoxEntities.reserve(64u);

            UpdateAnimatorBoundingBoxes(Ctx);
            if (IsDrawBoundingBoxesEnabled == true) {
                Ctx.ForEach<Animator, BoundingBox, EntityHierarchy>([&](Arche::EntityID EntityId, Animator& AnimatorComponent, BoundingBox& BoundingBoxComponent, EntityHierarchy& HierarchyComponent) {
                    (void)AnimatorComponent;
                    (void)HierarchyComponent;

                    if (BoundingBoxComponent.HasWorldObb() == false) {
                        return;
                    }

                    const bool IsInserted{ AppendedBoundingBoxEntities.insert(EntityId).second };
                    if (IsInserted == true) {
                        AppendBoundingBoxContext(BoundingBoxComponent.GetWorldObb(), GatherResult.GetBoundingBoxContexts());
                    }
                });
            }

            Ctx.ForEach<Transform, SkinnedMeshRenderer, EntityHierarchy>([&](Arche::EntityID EntityId, Transform& TransformComponent, SkinnedMeshRenderer& Renderer, EntityHierarchy& HierarchyComponent) {
                (void)HierarchyComponent;

                if (Renderer.model == nullptr || Renderer.active == false) {
                    return;
                }

                PreparedSkinnedMeshData PreparedData{};
                const bool IsPrepared{ TryBuildPreparedResult(Ctx, EntityId, IsDrawBoundingBoxesEnabled, PreparedData) };
                if (IsPrepared == false) {
                    return;
                }

                const std::vector<ModelNode>& Nodes{ Renderer.model->GetNodes() };
                if (Renderer.nodeIndex >= Nodes.size()) {
                    return;
                }

                const ModelNode& Node{ Nodes[Renderer.nodeIndex] };
                const std::vector<ModelSubMesh>& SubMeshes{ Node.GetSubMeshes() };
                if (SubMeshes.empty() == true || PreparedData.mBonePalette.empty() == true) {
                    return;
                }

                const std::uint32_t LocalBoneIndexStart{ static_cast<std::uint32_t>(GatherResult.GetBonePalette().size()) };
                GatherResult.GetBonePalette().insert(GatherResult.GetBonePalette().end(), PreparedData.mBonePalette.begin(), PreparedData.mBonePalette.end());
                if (IsDrawBoundingBoxesEnabled == true) {
                    GatherResult.GetBoundingBoxContexts().insert(GatherResult.GetBoundingBoxContexts().end(), PreparedData.mBoneBoundingBoxContexts.begin(), PreparedData.mBoneBoundingBoxContexts.end());
                }

                BoundingBox* BoundingBoxComponent{ Ctx.WriteComponent<BoundingBox>(EntityId) };
                if (IsDrawBoundingBoxesEnabled == true && BoundingBoxComponent != nullptr && BoundingBoxComponent->HasWorldObb() == true) {
                    const bool IsInserted{ AppendedBoundingBoxEntities.insert(EntityId).second };
                    if (IsInserted == true) {
                        AppendBoundingBoxContext(BoundingBoxComponent->GetWorldObb(), GatherResult.GetBoundingBoxContexts());
                    }
                }

                const Material* MaterialComponent{ Ctx.ReadComponent<Material>(EntityId) };
                const std::uint32_t MaterialFlags{ MaterialComponent == nullptr ? 0u : MaterialComponent->Flags };
                const bool IsPickedHierarchy{ IsEntityWithinPickedHierarchy(Ctx, EntityId, Ctx.GetPickedEntityId()) };
                const std::uint32_t PickFlags{ IsPickedHierarchy == true ? PickedDrawFlagBitMask : 0u };
                const RegisteredMaterialGroup* ResolvedMaterialGroup{ ResolveMaterialGroup(MaterialGroups, MaterialComponent) };

                RenderContract::ModelContext ModelContext{};
                ModelContext.mWorld = TransformComponent.worldMatrix;
                ModelContext.mPrevWorld = ModelContext.mWorld;
                ModelContext.mFlags = SkinnedModelContextFlagBitMask;
                ModelContext.mBoneIndexStart = LocalBoneIndexStart;
                ModelContext.mObjectId = static_cast<std::uint32_t>(GatherResult.GetModelContexts().size());
                GatherResult.GetModelContexts().push_back(ModelContext);
                AppendSkinnedDrawRecords(SubMeshes, Node, ResolvedMaterialGroup, ModelContext.mObjectId, MaterialFlags, PickFlags, GatherResult.GetDrawRecords());

                for (std::uint32_t CascadeIndex{}; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1u) {
                    const bool IsVisibleByShadow{ IsVisibleByShadowBox(Ctx, EntityId, ShadowCullingBoxes[CascadeIndex]) };
                    if (IsVisibleByShadow == false) {
                        continue;
                    }

                    RenderContract::ShadowRenderContext& ShadowRenderContext{ GatherResult.GetShadowRenderContexts()[CascadeIndex] };
                    RenderContract::ModelContext ShadowModelContext{};
                    ShadowModelContext.mWorld = TransformComponent.worldMatrix;
                    ShadowModelContext.mPrevWorld = ShadowModelContext.mWorld;
                    ShadowModelContext.mFlags = SkinnedModelContextFlagBitMask;
                    ShadowModelContext.mBoneIndexStart = LocalBoneIndexStart;
                    ShadowModelContext.mObjectId = static_cast<std::uint32_t>(ShadowRenderContext.mModelContexts.size());
                    ShadowRenderContext.mModelContexts.push_back(ShadowModelContext);
                    AppendSkinnedDrawRecords(SubMeshes, Node, ResolvedMaterialGroup, ShadowModelContext.mObjectId, MaterialFlags, 0u, ShadowRenderContext.mDrawRecords);
                }
            });
        }
    }
}
