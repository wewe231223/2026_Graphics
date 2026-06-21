#include "StaticRenderSystem.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "Game/Model/AssetRegistry.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Culling.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Base/Context.h"

namespace Game {
    namespace Pipeline {
        namespace {
            constexpr std::uint32_t PickedDrawFlagBitMask{ 0x1u };

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

            void AppendBoundingBoxContext(const DirectX::BoundingOrientedBox& WorldObb, PipelineContext& Ctx) {
                if (Ctx.HasRenderFlag(RenderContract::FrameGlobalFlagDrawBoundingBoxes) == false) {
                    return;
                }

                RenderContract::BoundingBoxContext BoundingBoxContext{};
                BoundingBoxContext.mCenter = SimpleMath::Vector4{ WorldObb.Center.x, WorldObb.Center.y, WorldObb.Center.z, 1.0f };
                BoundingBoxContext.mExtents = SimpleMath::Vector4{ WorldObb.Extents.x, WorldObb.Extents.y, WorldObb.Extents.z, 0.0f };
                BoundingBoxContext.mOrientation = SimpleMath::Vector4{ WorldObb.Orientation.x, WorldObb.Orientation.y, WorldObb.Orientation.z, WorldObb.Orientation.w };
                Ctx.GetRenderGatherResult().GetBoundingBoxContexts().push_back(BoundingBoxContext);
            }

            void AppendStaticDrawRecords(const std::vector<ModelSubMesh>& SubMeshes, const ModelNode& Node, const RegisteredMaterialGroup* ResolvedMaterialGroup, std::uint32_t ObjectIndex, std::uint32_t MaterialFlags, std::uint32_t PickFlags, std::vector<RenderContract::DrawRecord>& OutDrawRecords) {
                for (std::size_t SubMeshIndex{ 0 }; SubMeshIndex < SubMeshes.size(); ++SubMeshIndex) {
                    const ModelSubMesh& SubMesh{ SubMeshes[SubMeshIndex] };
                    const RenderContract::IPipeline* Pipeline{ nullptr };
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

            bool IsVisibleByFrustum(PipelineContext& Ctx, Arche::EntityID EntityId, const Frustum* CullingFrustumComponent) {
                const Culling* CullingComponent{ Ctx.ReadComponent<Culling>(EntityId) };
                if (CullingComponent != nullptr && CullingComponent->frustumCulling == false) {
                    return true;
                }

                if (CullingFrustumComponent == nullptr) {
                    return true;
                }

                const BoundingBox* BoundingBoxComponent{ Ctx.ReadComponent<BoundingBox>(EntityId) };
                if (BoundingBoxComponent == nullptr || BoundingBoxComponent->HasWorldObb() == false) {
                    return true;
                }

                return CullingFrustumComponent->Intersects(BoundingBoxComponent->GetWorldObb());
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
        }

        PipelineStaticRenderSystem::PipelineStaticRenderSystem() {
        }

        PipelineStaticRenderSystem::~PipelineStaticRenderSystem() {
        }

        PipelineStaticRenderSystem::PipelineStaticRenderSystem(const PipelineStaticRenderSystem& Other) {
            (void)Other;
        }

        PipelineStaticRenderSystem& PipelineStaticRenderSystem::operator=(const PipelineStaticRenderSystem& Other) {
            (void)Other;
            return *this;
        }

        PipelineStaticRenderSystem::PipelineStaticRenderSystem(PipelineStaticRenderSystem&& Other) noexcept {
            (void)Other;
        }

        PipelineStaticRenderSystem& PipelineStaticRenderSystem::operator=(PipelineStaticRenderSystem&& Other) noexcept {
            (void)Other;
            return *this;
        }

        const std::string& PipelineStaticRenderSystem::Name() const {
            static const std::string NameText{ "StaticRenderSystem" };
            return NameText;
        }

        void PipelineStaticRenderSystem::Execute(PipelineContext& Ctx, float Dt) {
            (void)Dt;

            RenderContract::RenderGatherResult& GatherResult{ Ctx.GetRenderGatherResult() };
            const std::vector<RegisteredMaterialGroup>* MaterialGroups{ Ctx.GetMaterialGroups() };
            const Frustum* CullingFrustumComponent{ Ctx.GetActiveCameraFrustum() };
            const RenderContract::ShadowMappingParameter& ShadowMappingParameter{ Ctx.GetShadowMappingParameter() };
            const std::uint32_t ShadowCascadeCount{ RenderContract::ResolveShadowCascadeCount(ShadowMappingParameter) };
            const std::array<DirectX::BoundingOrientedBox, RenderContract::ShadowCascadeMaxCount> ShadowCullingBoxes{ RenderContract::BuildShadowCullingBoxes(ShadowMappingParameter) };

            Ctx.ForEach<Transform, StaticMeshRenderer, EntityHierarchy>([&](Arche::EntityID EntityId, Transform& TransformComponent, StaticMeshRenderer& Renderer, EntityHierarchy& HierarchyComponent) {
                (void)HierarchyComponent;

                const Material* MaterialComponent{ Ctx.ReadComponent<Material>(EntityId) };
                if (Renderer.model == nullptr || Renderer.active == false) {
                    return;
                }

                const SimpleMath::Matrix NodeWorld{ TransformComponent.worldMatrix };
                BoundingBox* BoundingBoxComponent{ Ctx.WriteComponent<BoundingBox>(EntityId) };
                if (BoundingBoxComponent != nullptr && (TransformComponent.mWorldMatrixChanged == true || BoundingBoxComponent->HasWorldObb() == false)) {
                    BoundingBoxComponent->UpdateWorldObb(NodeWorld);
                }

                const std::vector<ModelNode>& Nodes{ Renderer.model->GetNodes() };
                if (Renderer.nodeIndex >= Nodes.size()) {
                    return;
                }

                const ModelNode& Node{ Nodes[Renderer.nodeIndex] };
                const std::vector<ModelSubMesh>& SubMeshes{ Node.GetSubMeshes() };
                if (SubMeshes.empty() == true) {
                    return;
                }

                const std::uint32_t MaterialFlags{ MaterialComponent == nullptr ? 0u : MaterialComponent->Flags };
                const bool IsPickedHierarchy{ IsEntityWithinPickedHierarchy(Ctx, EntityId, Ctx.GetPickedEntityId()) };
                const std::uint32_t PickFlags{ IsPickedHierarchy == true ? PickedDrawFlagBitMask : 0u };
                const RegisteredMaterialGroup* ResolvedMaterialGroup{ ResolveMaterialGroup(MaterialGroups, MaterialComponent) };
                const bool IsVisible{ IsVisibleByFrustum(Ctx, EntityId, CullingFrustumComponent) };

                if (IsVisible == true) {
                    RenderContract::ModelContext ModelContext{};
                    ModelContext.mWorld = NodeWorld;
                    ModelContext.mPrevWorld = ModelContext.mWorld;

                    if (BoundingBoxComponent != nullptr && BoundingBoxComponent->HasWorldObb() == true) {
                        AppendBoundingBoxContext(BoundingBoxComponent->GetWorldObb(), Ctx);
                    }

                    ModelContext.mObjectId = static_cast<std::uint32_t>(GatherResult.GetModelContexts().size());
                    GatherResult.GetModelContexts().push_back(ModelContext);
                    AppendStaticDrawRecords(SubMeshes, Node, ResolvedMaterialGroup, ModelContext.mObjectId, MaterialFlags, PickFlags, GatherResult.GetDrawRecords());
                }

                for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1u) {
                    const bool IsVisibleByShadow{ IsVisibleByShadowBox(Ctx, EntityId, ShadowCullingBoxes[CascadeIndex]) };
                    if (IsVisibleByShadow == false) {
                        continue;
                    }

                    RenderContract::ShadowRenderContext& ShadowRenderContext{ GatherResult.GetShadowRenderContexts()[CascadeIndex] };
                    RenderContract::ModelContext ShadowModelContext{};
                    ShadowModelContext.mWorld = NodeWorld;
                    ShadowModelContext.mPrevWorld = ShadowModelContext.mWorld;
                    ShadowModelContext.mObjectId = static_cast<std::uint32_t>(ShadowRenderContext.mModelContexts.size());
                    ShadowRenderContext.mModelContexts.push_back(ShadowModelContext);
                    AppendStaticDrawRecords(SubMeshes, Node, ResolvedMaterialGroup, ShadowModelContext.mObjectId, MaterialFlags, 0u, ShadowRenderContext.mDrawRecords);
                }
            });
        }
    }
}
