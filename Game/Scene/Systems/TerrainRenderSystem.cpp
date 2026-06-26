#include "TerrainRenderSystem.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Game/Model/AssetRegistry.h"
#include "Game/Model/TerrainRenderResource.h"
#include "Game/Scene/Base/Context.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Culling.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/TerrainRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "RenderContract/Writer/TerrainRenderWriter.h"
#include "Terrain/TerrainRenderDataBuilder.h"

namespace Game {
    namespace Pipeline {
        namespace {
            const RegisteredMaterialGroup* ResolveMaterialGroup(const std::vector<RegisteredMaterialGroup>* MaterialGroups, const Material* MaterialComponent) {
                if (MaterialGroups == nullptr || MaterialGroups->empty() == true) {
                    return nullptr;
                }

                std::size_t ResolvedMaterialGroupIndex{ MaterialComponent == nullptr ? 0U : MaterialComponent->MaterialGroupIndex };
                if (ResolvedMaterialGroupIndex >= MaterialGroups->size() || (*MaterialGroups)[ResolvedMaterialGroupIndex].Items.empty() == true) {
                    ResolvedMaterialGroupIndex = 0U;
                }

                if (ResolvedMaterialGroupIndex >= MaterialGroups->size() || (*MaterialGroups)[ResolvedMaterialGroupIndex].Items.empty() == true) {
                    return nullptr;
                }

                return &(*MaterialGroups)[ResolvedMaterialGroupIndex];
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

            std::vector<Terrain::TerrainRenderSubMeshBinding> BuildTerrainRenderSubMeshBindings(const ModelNode& Node, const RegisteredMaterialGroup* MaterialGroup) {
                std::vector<Terrain::TerrainRenderSubMeshBinding> Bindings{};
                Bindings.resize(Node.GetSubMeshes().size());
                if (MaterialGroup == nullptr) {
                    return Bindings;
                }

                for (std::size_t SubMeshIndex{}; SubMeshIndex < Node.GetSubMeshes().size(); SubMeshIndex += 1U) {
                    const ModelSubMesh& SubMesh{ Node.GetSubMesh(SubMeshIndex) };
                    std::size_t MaterialItemIndex{ SubMesh.mMaterialGroupItemIndex };
                    if (MaterialItemIndex >= MaterialGroup->Items.size()) {
                        MaterialItemIndex = 0U;
                    }

                    if (MaterialItemIndex >= MaterialGroup->Items.size()) {
                        continue;
                    }

                    const RegisteredMaterialGroupItem& MaterialItem{ MaterialGroup->Items[MaterialItemIndex] };
                    Terrain::TerrainRenderSubMeshBinding& Binding{ Bindings[SubMeshIndex] };
                    Binding.mPipeline = MaterialItem.Pipeline;
                    Binding.mMaterialIndex = MaterialItem.MaterialIndex;
                }

                return Bindings;
            }
        }

        PipelineTerrainRenderSystem::PipelineTerrainRenderSystem() {
        }

        PipelineTerrainRenderSystem::~PipelineTerrainRenderSystem() {
        }

        PipelineTerrainRenderSystem::PipelineTerrainRenderSystem(const PipelineTerrainRenderSystem& Other) {
            (void)Other;
        }

        PipelineTerrainRenderSystem& PipelineTerrainRenderSystem::operator=(const PipelineTerrainRenderSystem& Other) {
            (void)Other;
            return *this;
        }

        PipelineTerrainRenderSystem::PipelineTerrainRenderSystem(PipelineTerrainRenderSystem&& Other) noexcept {
            (void)Other;
        }

        PipelineTerrainRenderSystem& PipelineTerrainRenderSystem::operator=(PipelineTerrainRenderSystem&& Other) noexcept {
            (void)Other;
            return *this;
        }

        const std::string& PipelineTerrainRenderSystem::Name() const {
            static const std::string NameText{ "TerrainRenderSystem" };
            return NameText;
        }

        void PipelineTerrainRenderSystem::Execute(PipelineContext& Ctx, float Dt) {
            (void)Dt;

            RenderContract::TerrainRenderWriter TerrainWriter{ Ctx.GetRenderGatherResult() };
            const std::uint32_t FrameIndex{ Ctx.GetFrameIndex() };
            const std::vector<RegisteredMaterialGroup>* MaterialGroups{ Ctx.GetMaterialGroups() };
            const Frustum* CullingFrustumComponent{ Ctx.GetActiveCameraFrustum() };
            const RenderContract::ShadowMappingParameter& ShadowMappingParameter{ Ctx.GetShadowMappingParameter() };
            const std::uint32_t ShadowCascadeCount{ RenderContract::ResolveShadowCascadeCount(ShadowMappingParameter) };
            const std::array<DirectX::BoundingOrientedBox, RenderContract::ShadowCascadeMaxCount> ShadowCullingBoxes{ RenderContract::BuildShadowCullingBoxes(ShadowMappingParameter) };
            SimpleMath::Vector3 CameraPosition{};
            const bool HasCameraPosition{ Ctx.GetActiveCameraPosition(CameraPosition) };

            Ctx.ForEach<Transform, TerrainRenderer, EntityHierarchy>([&](Arche::EntityID EntityId, Transform& TransformComponent, TerrainRenderer& Renderer, EntityHierarchy& HierarchyComponent) {
                (void)HierarchyComponent;

                if (Renderer.mTileMetadataIndex != InvalidTerrainTileMetadataIndex || Renderer.mResource == nullptr || Renderer.mActive == false) {
                    return;
                }

                const std::shared_ptr<Model>& ModelData{ Renderer.mResource->GetModel() };
                if (ModelData == nullptr) {
                    return;
                }

                const ModelNode* NodePointer{ ModelData->GetRootNode() };
                if (NodePointer == nullptr || NodePointer->GetSubMeshes().empty() == true) {
                    return;
                }

                const Material* MaterialComponent{ Ctx.ReadComponent<Material>(EntityId) };
                const RegisteredMaterialGroup* MaterialGroup{ ResolveMaterialGroup(MaterialGroups, MaterialComponent) };
                const std::vector<Terrain::TerrainRenderSubMeshBinding> SubMeshBindings{ BuildTerrainRenderSubMeshBindings(*NodePointer, MaterialGroup) };
                BoundingBox* BoundingBoxComponent{ Ctx.WriteComponent<BoundingBox>(EntityId) };
                const bool HasCachedParentWorldBoundingBox{ TransformComponent.mWorldMatrixChanged == false && BoundingBoxComponent != nullptr && BoundingBoxComponent->HasWorldObb() == true };
                const TerrainRenderer* PickedTerrainRenderer{ Ctx.ReadComponent<TerrainRenderer>(Ctx.GetPickedEntityId()) };
                const bool IsPickedTileInThisTerrain{ PickedTerrainRenderer != nullptr && PickedTerrainRenderer->mResource == Renderer.mResource && PickedTerrainRenderer->mTileMetadataIndex != InvalidTerrainTileMetadataIndex && IsEntityWithinPickedHierarchy(Ctx, Ctx.GetPickedEntityId(), EntityId) };

                Terrain::TerrainRenderInput Input{};
                Input.mTileMetadataItems = &Renderer.mResource->GetTileMetadata();
                Input.mLodDistances = &Renderer.mResource->GetLodDistances();
                Input.mMesh = NodePointer;
                Input.mSubMeshBindings = &SubMeshBindings;
                Input.mTerrainUploadFuture = Renderer.mResource->GetFrameUploadFuture(FrameIndex);
                Input.mLocalBoundingBox = Renderer.mResource->GetLocalBoundingBox();
                Input.mWorld = TransformComponent.worldMatrix;
                Input.mPrevWorld = TransformComponent.mPrevWorldMatrix;
                Input.mCameraPosition = CameraPosition;
                Input.mCullingFrustum = CullingFrustumComponent == nullptr ? nullptr : &CullingFrustumComponent->mValue;
                Input.mShadowCullingBoxes = ShadowCullingBoxes;
                Input.mTileQuadCount = Renderer.mResource->GetTileQuadCount();
                Input.mTileCountX = Renderer.mResource->GetTileCountX();
                Input.mTileCountZ = Renderer.mResource->GetTileCountZ();
                Input.mLodCount = Renderer.mResource->GetLodCount();
                Input.mHeightFieldWidth = Renderer.mResource->GetHeightFieldWidth();
                Input.mHeightFieldHeight = Renderer.mResource->GetHeightFieldHeight();
                Input.mSplatMapWidth = Renderer.mResource->GetSplatMapWidth();
                Input.mSplatMapHeight = Renderer.mResource->GetSplatMapHeight();
                Input.mHeightFieldSrvDescriptorIndex = Renderer.mResource->GetHeightFieldSrvDescriptorIndex(FrameIndex);
                Input.mSplatMapSrvDescriptorIndices[0] = Renderer.mResource->GetSplatMapSrvDescriptorIndex(FrameIndex, 0u);
                Input.mSplatMapSrvDescriptorIndices[1] = Renderer.mResource->GetSplatMapSrvDescriptorIndex(FrameIndex, 1u);
                Input.mShadowCascadeCount = ShadowCascadeCount;
                Input.mFrameIndex = FrameIndex;
                Input.mMaterialFlags = MaterialComponent == nullptr ? 0U : MaterialComponent->Flags;
                Input.mPickedTileMetadataIndex = IsPickedTileInThisTerrain == true ? PickedTerrainRenderer->mTileMetadataIndex : InvalidTerrainTileMetadataIndex;
                Input.mStreamOriginGridX = Renderer.mResource->GetStreamOriginGridX();
                Input.mStreamOriginGridZ = Renderer.mResource->GetStreamOriginGridZ();
                Input.mLodExponent = Renderer.mResource->GetLodExponent();
                Input.mMaxHeight = Renderer.mResource->GetMaxHeight();
                Input.mCellSizeX = Renderer.mResource->GetCellSizeX();
                Input.mCellSizeZ = Renderer.mResource->GetCellSizeZ();
                Input.mOriginOffsetX = Renderer.mResource->GetOriginOffsetX();
                Input.mOriginOffsetZ = Renderer.mResource->GetOriginOffsetZ();
                Input.mHasCameraPosition = HasCameraPosition;
                Input.mHasParentWorldBoundingBox = HasCachedParentWorldBoundingBox;
                Input.mIsFrustumCullingEnabled = Ctx.ReadComponent<Culling>(EntityId) == nullptr || Ctx.ReadComponent<Culling>(EntityId)->frustumCulling;
                Input.mIsDrawBoundingBoxesEnabled = Ctx.HasRenderFlag(RenderContract::FrameGlobalFlagDrawBoundingBoxes);
                Input.mIsPickedParentHierarchy = IsEntityWithinPickedHierarchy(Ctx, EntityId, Ctx.GetPickedEntityId());
                Input.mHeightFieldFlipV = Renderer.mResource->IsHeightFieldFlipV();

                if (HasCachedParentWorldBoundingBox == true) {
                    Input.mParentWorldBoundingBox = BoundingBoxComponent->GetWorldObb();
                }

                const Terrain::TerrainRenderResult Result{ Terrain::WriteTerrainRenderData(Input, TerrainWriter) };
                if (BoundingBoxComponent != nullptr && Result.mHasParentWorldBoundingBox == true && (TransformComponent.mWorldMatrixChanged == true || BoundingBoxComponent->HasWorldObb() == false)) {
                    BoundingBoxComponent->SetWorldObb(Result.mParentWorldBoundingBox);
                }
            });
        }
    }
}
