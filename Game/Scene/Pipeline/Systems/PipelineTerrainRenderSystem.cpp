#include "PipelineTerrainRenderSystem.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "Game/Model/AssetRegistry.h"
#include "Game/Model/TerrainRenderResource.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Culling.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/TerrainRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Pipeline/PipelineContext.h"

namespace Game {
    namespace Pipeline {
        namespace {
            constexpr std::uint32_t PickedDrawFlagBitMask{ 0x1u };
            constexpr std::uint32_t InvalidSrvDescriptorIndex{ 0xffffffffu };
            constexpr std::uint32_t TerrainEdgeNegativeXIndex{ 0u };
            constexpr std::uint32_t TerrainEdgeNegativeZIndex{ 1u };
            constexpr std::uint32_t TerrainEdgePositiveXIndex{ 2u };
            constexpr std::uint32_t TerrainEdgePositiveZIndex{ 3u };
            constexpr std::array<std::uint32_t, 8> TerrainTessFactorDivisors{ 1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u };

            struct TerrainTileTessellationData final {
            public:
                float mBaseTessFactor{ 1.0f };
                std::array<float, 4> mOuterTessFactors{ 1.0f, 1.0f, 1.0f, 1.0f };
                std::array<float, 2> mInsideTessFactors{ 1.0f, 1.0f };
            };

            std::uint32_t CalculateTerrainTileLinearIndex(std::uint32_t TileCountX, std::uint32_t TileIndexX, std::uint32_t TileIndexZ) {
                return (TileIndexZ * TileCountX) + TileIndexX;
            }

            float CalculateTerrainTessFactor(std::uint32_t TileQuadCount, std::uint32_t LodIndex) {
                const std::uint32_t BaseFactor{ std::max(TileQuadCount, 1u) };
                const std::uint32_t DivisorIndex{ std::min(LodIndex, static_cast<std::uint32_t>(TerrainTessFactorDivisors.size() - 1u)) };
                const std::uint32_t Factor{ std::max(BaseFactor / TerrainTessFactorDivisors[DivisorIndex], 1u) };
                return static_cast<float>(Factor);
            }

            float ResolveTerrainMaxLodDistance(const std::vector<float>& LodDistances) {
                float MaxLodDistance{};
                for (const float LodDistance : LodDistances) {
                    MaxLodDistance = std::max(MaxLodDistance, LodDistance);
                }

                return MaxLodDistance;
            }

            float CalculateTerrainExponentialLodRatio(float Distance, float MaxDistance, float LodExponent) {
                if (MaxDistance <= 0.0f) {
                    return 0.0f;
                }

                if (LodExponent <= 0.0f) {
                    return std::clamp(Distance / MaxDistance, 0.0f, 1.0f);
                }

                const float NormalizedDistance{ std::clamp(Distance / MaxDistance, 0.0f, 1.0f) };
                const float ExponentDenominator{ static_cast<float>(std::exp(static_cast<double>(LodExponent)) - 1.0) };
                if (ExponentDenominator <= 0.0f) {
                    return NormalizedDistance;
                }

                const double ExponentValue{ static_cast<double>(LodExponent) * static_cast<double>(NormalizedDistance) };
                const float ExponentNumerator{ static_cast<float>(std::exp(ExponentValue) - 1.0) };
                return std::clamp(ExponentNumerator / ExponentDenominator, 0.0f, 1.0f);
            }

            std::uint32_t SelectLodIndex(const TerrainTileMetadata& TileMetadata, const SimpleMath::Matrix& WorldMatrix, const SimpleMath::Vector3& CameraPosition, bool HasCameraPosition, const TerrainRenderResource& Resource) {
                const std::size_t AvailableLodCount{ static_cast<std::size_t>(Resource.GetLodCount()) };
                if (AvailableLodCount <= 1u || HasCameraPosition == false) {
                    return 0u;
                }

                const std::vector<float>& LodDistances{ Resource.GetLodDistances() };
                if (LodDistances.empty() == true) {
                    return 0u;
                }

                const SimpleMath::Vector3 WorldCenter{ SimpleMath::Vector3::Transform(TileMetadata.mCenter, WorldMatrix) };
                const SimpleMath::Vector3 CenterToCamera{ WorldCenter - CameraPosition };
                const float DistanceSquared{ CenterToCamera.LengthSquared() };
                const float MaxLodDistance{ ResolveTerrainMaxLodDistance(LodDistances) };
                if (MaxLodDistance <= 0.0f) {
                    return 0u;
                }

                const float Distance{ std::sqrt(DistanceSquared) };
                const float LodRatio{ CalculateTerrainExponentialLodRatio(Distance, MaxLodDistance, Resource.GetLodExponent()) };
                const float MaxLodIndex{ static_cast<float>(AvailableLodCount - 1u) };
                const float SelectedLodValue{ std::clamp(LodRatio * MaxLodIndex, 0.0f, MaxLodIndex) };
                return static_cast<std::uint32_t>(SelectedLodValue);
            }

            void SetTerrainInsideTessFactors(TerrainTileTessellationData& TessellationData) {
                TessellationData.mInsideTessFactors[0] = TessellationData.mBaseTessFactor;
                TessellationData.mInsideTessFactors[1] = TessellationData.mBaseTessFactor;
            }

            void MatchTerrainSharedEdge(TerrainTileTessellationData& FirstTessellationData, std::uint32_t FirstEdgeIndex, TerrainTileTessellationData& SecondTessellationData, std::uint32_t SecondEdgeIndex) {
                const float SharedFactor{ std::max(FirstTessellationData.mOuterTessFactors[FirstEdgeIndex], SecondTessellationData.mOuterTessFactors[SecondEdgeIndex]) };
                FirstTessellationData.mOuterTessFactors[FirstEdgeIndex] = SharedFactor;
                SecondTessellationData.mOuterTessFactors[SecondEdgeIndex] = SharedFactor;
            }

            RFD::TerrainPatchContext BuildTerrainPatchContext(const TerrainTileMetadata& TileMetadata, const TerrainRenderResource& Resource, const TerrainTileTessellationData& TessellationData, std::uint32_t FrameIndex) {
                RFD::TerrainPatchContext PatchContext{};
                PatchContext.OuterTessFactors = SimpleMath::Vector4{ TessellationData.mOuterTessFactors[0], TessellationData.mOuterTessFactors[1], TessellationData.mOuterTessFactors[2], TessellationData.mOuterTessFactors[3] };
                PatchContext.InsideTessFactors = SimpleMath::Vector4{ TessellationData.mInsideTessFactors[0], TessellationData.mInsideTessFactors[1], 0.0f, 0.0f };
                PatchContext.TileGrid = SimpleMath::Vector4{ static_cast<float>(TileMetadata.mStartX), static_cast<float>(TileMetadata.mStartZ), static_cast<float>(TileMetadata.mQuadCountX), static_cast<float>(TileMetadata.mQuadCountZ) };
                PatchContext.HeightFieldParameters = SimpleMath::Vector4{ static_cast<float>(Resource.GetHeightFieldWidth()), static_cast<float>(Resource.GetHeightFieldHeight()), Resource.GetMaxHeight(), Resource.IsHeightFieldFlipV() == true ? 1.0f : 0.0f };
                PatchContext.TerrainParameters = SimpleMath::Vector4{ Resource.GetCellSizeX(), Resource.GetCellSizeZ(), Resource.GetOriginOffsetX(), Resource.GetOriginOffsetZ() };
                PatchContext.mTerrainUvParameters = SimpleMath::Vector4{ static_cast<float>(Resource.GetStreamOriginGridX()), static_cast<float>(Resource.GetStreamOriginGridZ()), 0.0f, 0.0f };
                PatchContext.HeightFieldSrvDescriptorIndex = Resource.GetHeightFieldSrvDescriptorIndex(FrameIndex);
                PatchContext.SplatMapSrvDescriptorIndex = Resource.GetSplatMapSrvDescriptorIndex(FrameIndex);
                PatchContext.SplatMapWidth = Resource.GetSplatMapWidth();
                PatchContext.SplatMapHeight = Resource.GetSplatMapHeight();
                return PatchContext;
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

            void AppendBoundingBoxContext(const DirectX::BoundingOrientedBox& WorldObb, PipelineContext& Ctx) {
                if (Ctx.HasRenderFlag(RFD::FrameGlobalFlagDrawBoundingBoxes) == false) {
                    return;
                }

                RFD::BoundingBoxContext BoundingBoxContext{};
                BoundingBoxContext.center = SimpleMath::Vector4{ WorldObb.Center.x, WorldObb.Center.y, WorldObb.Center.z, 1.0f };
                BoundingBoxContext.extents = SimpleMath::Vector4{ WorldObb.Extents.x, WorldObb.Extents.y, WorldObb.Extents.z, 0.0f };
                BoundingBoxContext.orientation = SimpleMath::Vector4{ WorldObb.Orientation.x, WorldObb.Orientation.y, WorldObb.Orientation.z, WorldObb.Orientation.w };
                Ctx.GetRenderGatherResult().GetBoundingBoxContexts().push_back(BoundingBoxContext);
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

            bool IsWorldBoundingBoxVisibleByFrustum(const DirectX::BoundingOrientedBox& WorldBoundingBox, const Frustum* CullingFrustumComponent, bool IsFrustumCullingEnabled) {
                if (IsFrustumCullingEnabled == false) {
                    return true;
                }

                if (CullingFrustumComponent == nullptr) {
                    return true;
                }

                return CullingFrustumComponent->Intersects(WorldBoundingBox);
            }

            bool IsWorldBoundingBoxVisibleByShadowBox(const DirectX::BoundingOrientedBox& WorldBoundingBox, const DirectX::BoundingOrientedBox& CullingBox, bool IsFrustumCullingEnabled) {
                if (IsFrustumCullingEnabled == false) {
                    return true;
                }

                return CullingBox.Intersects(WorldBoundingBox);
            }

            bool IsTileVisibleByFrustum(const DirectX::BoundingOrientedBox& LocalBoundingBox, const SimpleMath::Matrix& WorldMatrix, const Frustum* CullingFrustumComponent, bool IsFrustumCullingEnabled, DirectX::BoundingOrientedBox& OutWorldBoundingBox) {
                LocalBoundingBox.Transform(OutWorldBoundingBox, WorldMatrix);
                if (IsFrustumCullingEnabled == false) {
                    return true;
                }

                if (CullingFrustumComponent == nullptr) {
                    return true;
                }

                return CullingFrustumComponent->Intersects(OutWorldBoundingBox);
            }

            void AppendTerrainDrawRecord(const TerrainTileMetadata& TileMetadata, const TerrainRenderResource& Resource, const ModelNode& Node, const RegisteredMaterialGroup* ResolvedMaterialGroup, std::uint32_t ObjectIndex, std::uint32_t MaterialFlags, std::uint32_t PickFlags, const TerrainTileTessellationData& TessellationData, std::uint32_t FrameIndex, std::vector<RFD::TerrainPatchContext>& OutTerrainPatchContexts, std::vector<RFD::DrawRecord>& OutDrawRecords) {
                const std::uint32_t TileSubMeshIndex{ TileMetadata.mSubMeshIndex };
                const ModelSubMesh& SubMesh{ Node.GetSubMesh(TileSubMeshIndex) };
                const Interface::IPipeline* Pipeline{ nullptr };
                std::uint32_t ResolvedMaterialIndex{};

                if (ResolvedMaterialGroup != nullptr) {
                    std::size_t ResolvedItemIndex{ SubMesh.MaterialGroupItemIndex };
                    if (ResolvedItemIndex >= ResolvedMaterialGroup->Items.size()) {
                        ResolvedItemIndex = 0u;
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
                DrawRecord.submesh = TileSubMeshIndex;
                DrawRecord.pass = 0u;
                DrawRecord.objectIndex = ObjectIndex;
                DrawRecord.materialIndex = ResolvedMaterialIndex;
                DrawRecord.flags = MaterialFlags | PickFlags;
                DrawRecord.TerrainPatchContextIndex = static_cast<std::uint32_t>(OutTerrainPatchContexts.size());
                DrawRecord.pad0 = 0u;
                OutTerrainPatchContexts.push_back(BuildTerrainPatchContext(TileMetadata, Resource, TessellationData, FrameIndex));
                OutDrawRecords.push_back(DrawRecord);
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

            RenderGatherResult& GatherResult{ Ctx.GetRenderGatherResult() };
            const std::uint32_t FrameIndex{ Ctx.GetFrameIndex() };
            const std::vector<RegisteredMaterialGroup>* MaterialGroups{ Ctx.GetMaterialGroups() };
            const Frustum* CullingFrustumComponent{ Ctx.GetActiveCameraFrustum() };
            const RFD::ShadowMappingParameter& ShadowMappingParameter{ Ctx.GetShadowMappingParameter() };
            const std::uint32_t ShadowCascadeCount{ RFD::ResolveShadowCascadeCount(ShadowMappingParameter) };
            const std::array<DirectX::BoundingOrientedBox, RFD::ShadowCascadeMaxCount> ShadowCullingBoxes{ RFD::BuildShadowCullingBoxes(ShadowMappingParameter) };
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

                if (Renderer.mResource->GetHeightFieldSrvDescriptorIndex(FrameIndex) == InvalidSrvDescriptorIndex || Renderer.mResource->GetSplatMapSrvDescriptorIndex(FrameIndex) == InvalidSrvDescriptorIndex) {
                    return;
                }

                const Interface::Future TerrainUploadFuture{ Renderer.mResource->GetFrameUploadFuture(FrameIndex) };
                if (TerrainUploadFuture.IsValid() == true) {
                    GatherResult.SetTerrainUploadFuture(TerrainUploadFuture);
                }

                const SimpleMath::Matrix NodeWorld{ TransformComponent.worldMatrix };
                const Culling* CullingComponent{ Ctx.ReadComponent<Culling>(EntityId) };
                const bool IsFrustumCullingEnabled{ CullingComponent == nullptr ? true : CullingComponent->frustumCulling };
                BoundingBox* BoundingBoxComponent{ Ctx.WriteComponent<BoundingBox>(EntityId) };
                DirectX::BoundingOrientedBox ParentWorldBoundingBox{};
                bool IsParentVisible{};
                if (TransformComponent.mWorldMatrixChanged == false && BoundingBoxComponent != nullptr && BoundingBoxComponent->HasWorldObb() == true) {
                    ParentWorldBoundingBox = BoundingBoxComponent->GetWorldObb();
                    IsParentVisible = IsWorldBoundingBoxVisibleByFrustum(ParentWorldBoundingBox, CullingFrustumComponent, IsFrustumCullingEnabled);
                }
                else {
                    IsParentVisible = IsTileVisibleByFrustum(Renderer.mResource->GetLocalBoundingBox(), NodeWorld, CullingFrustumComponent, IsFrustumCullingEnabled, ParentWorldBoundingBox);
                }

                if (BoundingBoxComponent != nullptr && (TransformComponent.mWorldMatrixChanged == true || BoundingBoxComponent->HasWorldObb() == false)) {
                    BoundingBoxComponent->SetWorldObb(ParentWorldBoundingBox);
                }

                std::array<bool, RFD::ShadowCascadeMaxCount> IsParentVisibleByShadowCascade{};
                bool HasVisibleShadowParent{};
                for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1u) {
                    IsParentVisibleByShadowCascade[CascadeIndex] = IsWorldBoundingBoxVisibleByShadowBox(ParentWorldBoundingBox, ShadowCullingBoxes[CascadeIndex], IsFrustumCullingEnabled);
                    if (IsParentVisibleByShadowCascade[CascadeIndex] == true) {
                        HasVisibleShadowParent = true;
                    }
                }

                if (IsParentVisible == false && HasVisibleShadowParent == false) {
                    return;
                }

                if (IsParentVisible == true && BoundingBoxComponent != nullptr && BoundingBoxComponent->HasWorldObb() == true) {
                    AppendBoundingBoxContext(BoundingBoxComponent->GetWorldObb(), Ctx);
                }

                const std::vector<TerrainTileMetadata>& TileMetadataItems{ Renderer.mResource->GetTileMetadata() };
                std::vector<TerrainTileTessellationData> TileTessellationItems{};
                TileTessellationItems.resize(TileMetadataItems.size());
                for (std::size_t TileMetadataIndex{}; TileMetadataIndex < TileMetadataItems.size(); ++TileMetadataIndex) {
                    const TerrainTileMetadata& TileMetadata{ TileMetadataItems[TileMetadataIndex] };
                    const std::uint32_t SelectedLodIndex{ SelectLodIndex(TileMetadata, NodeWorld, CameraPosition, HasCameraPosition, *Renderer.mResource) };
                    const float TessFactor{ CalculateTerrainTessFactor(Renderer.mResource->GetTileQuadCount(), SelectedLodIndex) };
                    TerrainTileTessellationData& TessellationData{ TileTessellationItems[TileMetadataIndex] };
                    TessellationData.mBaseTessFactor = TessFactor;
                    TessellationData.mOuterTessFactors[TerrainEdgeNegativeXIndex] = TessFactor;
                    TessellationData.mOuterTessFactors[TerrainEdgeNegativeZIndex] = TessFactor;
                    TessellationData.mOuterTessFactors[TerrainEdgePositiveXIndex] = TessFactor;
                    TessellationData.mOuterTessFactors[TerrainEdgePositiveZIndex] = TessFactor;
                }

                for (std::size_t TileMetadataIndex{}; TileMetadataIndex < TileMetadataItems.size(); ++TileMetadataIndex) {
                    const TerrainTileMetadata& TileMetadata{ TileMetadataItems[TileMetadataIndex] };
                    if (TileMetadata.mTileIndexX + 1u < Renderer.mResource->GetTileCountX()) {
                        const std::uint32_t NeighborIndex{ CalculateTerrainTileLinearIndex(Renderer.mResource->GetTileCountX(), TileMetadata.mTileIndexX + 1u, TileMetadata.mTileIndexZ) };
                        if (NeighborIndex < TileTessellationItems.size()) {
                            MatchTerrainSharedEdge(TileTessellationItems[TileMetadataIndex], TerrainEdgePositiveXIndex, TileTessellationItems[NeighborIndex], TerrainEdgeNegativeXIndex);
                        }
                    }

                    if (TileMetadata.mTileIndexZ + 1u < Renderer.mResource->GetTileCountZ()) {
                        const std::uint32_t NeighborIndex{ CalculateTerrainTileLinearIndex(Renderer.mResource->GetTileCountX(), TileMetadata.mTileIndexX, TileMetadata.mTileIndexZ + 1u) };
                        if (NeighborIndex < TileTessellationItems.size()) {
                            MatchTerrainSharedEdge(TileTessellationItems[TileMetadataIndex], TerrainEdgePositiveZIndex, TileTessellationItems[NeighborIndex], TerrainEdgeNegativeZIndex);
                        }
                    }
                }

                for (TerrainTileTessellationData& TessellationData : TileTessellationItems) {
                    SetTerrainInsideTessFactors(TessellationData);
                }

                const Material* MaterialComponent{ Ctx.ReadComponent<Material>(EntityId) };
                const std::uint32_t MaterialFlags{ MaterialComponent == nullptr ? 0u : MaterialComponent->Flags };
                const TerrainRenderer* PickedTerrainRenderer{ Ctx.ReadComponent<TerrainRenderer>(Ctx.GetPickedEntityId()) };
                const bool IsPickedParentHierarchy{ IsEntityWithinPickedHierarchy(Ctx, EntityId, Ctx.GetPickedEntityId()) };
                const bool IsPickedTileInThisTerrain{ PickedTerrainRenderer != nullptr && PickedTerrainRenderer->mResource == Renderer.mResource && PickedTerrainRenderer->mTileMetadataIndex != InvalidTerrainTileMetadataIndex && IsEntityWithinPickedHierarchy(Ctx, Ctx.GetPickedEntityId(), EntityId) };
                const std::uint32_t PickedTileMetadataIndex{ IsPickedTileInThisTerrain == true ? PickedTerrainRenderer->mTileMetadataIndex : InvalidTerrainTileMetadataIndex };
                const RegisteredMaterialGroup* ResolvedMaterialGroup{ ResolveMaterialGroup(MaterialGroups, MaterialComponent) };
                bool HasModelContext{};
                std::uint32_t ObjectIndex{};
                std::array<bool, RFD::ShadowCascadeMaxCount> HasShadowModelContexts{};
                std::array<std::uint32_t, RFD::ShadowCascadeMaxCount> ShadowObjectIndices{};

                for (std::size_t TileMetadataIndex{}; TileMetadataIndex < TileMetadataItems.size(); ++TileMetadataIndex) {
                    const TerrainTileMetadata& TileMetadata{ TileMetadataItems[TileMetadataIndex] };
                    const std::uint32_t TileSubMeshIndex{ TileMetadata.mSubMeshIndex };
                    if (TileSubMeshIndex >= NodePointer->GetSubMeshes().size()) {
                        continue;
                    }

                    DirectX::BoundingOrientedBox TileWorldBoundingBox{};
                    TileMetadata.mLocalBoundingBox.Transform(TileWorldBoundingBox, NodeWorld);
                    const bool IsVisible{ IsWorldBoundingBoxVisibleByFrustum(TileWorldBoundingBox, CullingFrustumComponent, IsFrustumCullingEnabled) };
                    const bool IsPickedTile{ IsPickedParentHierarchy == true || PickedTileMetadataIndex == static_cast<std::uint32_t>(TileMetadataIndex) };

                    if (IsVisible == true) {
                        if (HasModelContext == false) {
                            RFD::ModelContext ModelContext{};
                            ModelContext.world = NodeWorld;
                            ModelContext.prevWorld = ModelContext.world;
                            ModelContext.objectID = static_cast<std::uint32_t>(GatherResult.GetModelContexts().size());
                            ObjectIndex = ModelContext.objectID;
                            GatherResult.GetModelContexts().push_back(ModelContext);
                            HasModelContext = true;
                        }

                        AppendBoundingBoxContext(TileWorldBoundingBox, Ctx);
                        AppendTerrainDrawRecord(TileMetadata, *Renderer.mResource, *NodePointer, ResolvedMaterialGroup, ObjectIndex, MaterialFlags, IsPickedTile == true ? PickedDrawFlagBitMask : 0u, TileTessellationItems[TileMetadataIndex], FrameIndex, GatherResult.GetTerrainPatchContexts(), GatherResult.GetDrawRecords());
                    }

                    for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1u) {
                        if (IsParentVisibleByShadowCascade[CascadeIndex] == false) {
                            continue;
                        }

                        const bool IsVisibleByShadow{ IsWorldBoundingBoxVisibleByShadowBox(TileWorldBoundingBox, ShadowCullingBoxes[CascadeIndex], IsFrustumCullingEnabled) };
                        if (IsVisibleByShadow == false) {
                            continue;
                        }

                        RFD::ShadowRenderContext& ShadowRenderContext{ GatherResult.GetShadowRenderContexts()[CascadeIndex] };
                        if (HasShadowModelContexts[CascadeIndex] == false) {
                            RFD::ModelContext ShadowModelContext{};
                            ShadowModelContext.world = NodeWorld;
                            ShadowModelContext.prevWorld = ShadowModelContext.world;
                            ShadowModelContext.objectID = static_cast<std::uint32_t>(ShadowRenderContext.ModelContexts.size());
                            ShadowObjectIndices[CascadeIndex] = ShadowModelContext.objectID;
                            ShadowRenderContext.ModelContexts.push_back(ShadowModelContext);
                            HasShadowModelContexts[CascadeIndex] = true;
                        }

                        AppendTerrainDrawRecord(TileMetadata, *Renderer.mResource, *NodePointer, ResolvedMaterialGroup, ShadowObjectIndices[CascadeIndex], MaterialFlags, 0u, TileTessellationItems[TileMetadataIndex], FrameIndex, ShadowRenderContext.TerrainPatchContexts, ShadowRenderContext.DrawRecords);
                    }
                }
            });
        }
    }
}
