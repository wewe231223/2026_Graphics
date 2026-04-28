#include "TerrainRenderSystem.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include <utility>
#include "Game/Model/AssetRegistry.h"
#include "Game/Model/TerrainRenderResource.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/Culling.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Frustum.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/TerrainRenderer.h"
#include "Game/Scene/Components/Transform.h"

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
        float BaseTessFactor{ 1.0f };
        std::array<float, 4> OuterTessFactors{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::array<float, 2> InsideTessFactors{ 1.0f, 1.0f };
    };

    std::uint32_t CalculateTerrainTileLinearIndex(std::uint32_t TileCountX, std::uint32_t TileIndexX, std::uint32_t TileIndexZ) {
        return (TileIndexZ * TileCountX) + TileIndexX;
    }

    float CalculateTerrainTessFactor(std::uint32_t TileQuadCount, std::uint32_t LodIndex) {
        const std::uint32_t BaseFactor{ (std::max)(TileQuadCount, 1u) };
        const std::uint32_t DivisorIndex{ (std::min)(LodIndex, static_cast<std::uint32_t>(TerrainTessFactorDivisors.size() - 1ULL)) };
        const std::uint32_t Factor{ (std::max)(BaseFactor / TerrainTessFactorDivisors[DivisorIndex], 1u) };

        return static_cast<float>(Factor);
    }

    void SetTerrainInsideTessFactors(TerrainTileTessellationData& TessellationData) {
        TessellationData.InsideTessFactors[0] = TessellationData.BaseTessFactor;
        TessellationData.InsideTessFactors[1] = TessellationData.BaseTessFactor;
    }

    void MatchTerrainSharedEdge(TerrainTileTessellationData& FirstTessellationData, std::uint32_t FirstEdgeIndex, TerrainTileTessellationData& SecondTessellationData, std::uint32_t SecondEdgeIndex) {
        const float SharedFactor{ (std::max)(FirstTessellationData.OuterTessFactors[FirstEdgeIndex], SecondTessellationData.OuterTessFactors[SecondEdgeIndex]) };
        FirstTessellationData.OuterTessFactors[FirstEdgeIndex] = SharedFactor;
        SecondTessellationData.OuterTessFactors[SecondEdgeIndex] = SharedFactor;
    }

    Game::RFD::TerrainPatchContext BuildTerrainPatchContext(const Game::TerrainTileMetadata& TileMetadata, const Game::TerrainRenderResource& Resource, const TerrainTileTessellationData& TessellationData) {
        Game::RFD::TerrainPatchContext PatchContext{};
        PatchContext.OuterTessFactors = SimpleMath::Vector4{ TessellationData.OuterTessFactors[0], TessellationData.OuterTessFactors[1], TessellationData.OuterTessFactors[2], TessellationData.OuterTessFactors[3] };
        PatchContext.InsideTessFactors = SimpleMath::Vector4{ TessellationData.InsideTessFactors[0], TessellationData.InsideTessFactors[1], 0.0f, 0.0f };
        PatchContext.TileGrid = SimpleMath::Vector4{ static_cast<float>(TileMetadata.mStartX), static_cast<float>(TileMetadata.mStartZ), static_cast<float>(TileMetadata.mQuadCountX), static_cast<float>(TileMetadata.mQuadCountZ) };
        PatchContext.HeightFieldParameters = SimpleMath::Vector4{ static_cast<float>(Resource.GetHeightFieldWidth()), static_cast<float>(Resource.GetHeightFieldHeight()), Resource.GetMaxHeight(), Resource.IsHeightFieldFlipV() == true ? 1.0f : 0.0f };
        PatchContext.TerrainParameters = SimpleMath::Vector4{ Resource.GetCellSizeX(), Resource.GetCellSizeZ(), Resource.GetOriginOffsetX(), Resource.GetOriginOffsetZ() };
        PatchContext.HeightFieldSrvDescriptorIndex = Resource.GetHeightFieldSrvDescriptorIndex();
        return PatchContext;
    }

    SimpleMath::Matrix BuildLocalWorldMatrix(const Game::Transform& TransformComponent) {
        const SimpleMath::Matrix TrsMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
        return TransformComponent.nodeToParent * TrsMatrix;
    }

    bool TryBuildWorldMatrix(Arche::World& World, Arche::EntityID EntityId, SimpleMath::Matrix& OutWorldMatrix) {
        std::vector<Arche::EntityID> EntityPath{};
        Arche::EntityID CurrentEntityId{ EntityId };

        while (CurrentEntityId != Arche::NullEntityID) {
            const Game::Transform* TransformComponent{ std::as_const(World).GetComponent<Game::Transform>(CurrentEntityId) };
            const Game::EntityHierarchy* HierarchyComponent{ std::as_const(World).GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (TransformComponent == nullptr || HierarchyComponent == nullptr) {
                return false;
            }

            EntityPath.push_back(CurrentEntityId);
            CurrentEntityId = HierarchyComponent->parent;
        }

        SimpleMath::Matrix ParentWorldMatrix{ SimpleMath::Matrix::Identity };
        for (std::vector<Arche::EntityID>::const_reverse_iterator EntityPathIter{ EntityPath.crbegin() }; EntityPathIter != EntityPath.crend(); ++EntityPathIter) {
            const Arche::EntityID CurrentPathEntityId{ *EntityPathIter };
            const Game::Transform* TransformComponent{ std::as_const(World).GetComponent<Game::Transform>(CurrentPathEntityId) };
            if (TransformComponent == nullptr) {
                return false;
            }

            const SimpleMath::Matrix LocalWorldMatrix{ BuildLocalWorldMatrix(*TransformComponent) };
            const SimpleMath::Matrix CurrentWorldMatrix{ LocalWorldMatrix * ParentWorldMatrix };
            ParentWorldMatrix = CurrentWorldMatrix;
        }

        OutWorldMatrix = ParentWorldMatrix;
        return true;
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

            const Game::EntityHierarchy* Hierarchy{ std::as_const(World).GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (Hierarchy == nullptr) {
                break;
            }

            CurrentEntityId = Hierarchy->parent;
        }

        return false;
    }

    void AppendBoundingBoxContext(const DirectX::BoundingOrientedBox& WorldObb, Game::RFD::RenderFrameData& RenderData) {
        Game::RFD::BoundingBoxContext BoundingBoxContext{};
        BoundingBoxContext.center = SimpleMath::Vector4{ WorldObb.Center.x, WorldObb.Center.y, WorldObb.Center.z, 1.0f };
        BoundingBoxContext.extents = SimpleMath::Vector4{ WorldObb.Extents.x, WorldObb.Extents.y, WorldObb.Extents.z, 0.0f };
        BoundingBoxContext.orientation = SimpleMath::Vector4{ WorldObb.Orientation.x, WorldObb.Orientation.y, WorldObb.Orientation.z, WorldObb.Orientation.w };
        RenderData.boundingBoxContexts.push_back(BoundingBoxContext);
    }

    const Game::RegisteredMaterialGroup* ResolveMaterialGroup(const std::vector<Game::RegisteredMaterialGroup>& MaterialGroups, const Game::Material* MaterialComponent) {
        if (MaterialGroups.empty() == true) {
            return nullptr;
        }

        std::size_t ResolvedMaterialGroupIndex{ MaterialComponent == nullptr ? 0u : MaterialComponent->MaterialGroupIndex };
        if (ResolvedMaterialGroupIndex >= MaterialGroups.size() || MaterialGroups[ResolvedMaterialGroupIndex].Items.empty()) {
            ResolvedMaterialGroupIndex = 0;
        }

        if (ResolvedMaterialGroupIndex >= MaterialGroups.size() || MaterialGroups[ResolvedMaterialGroupIndex].Items.empty() == true) {
            return nullptr;
        }

        return &MaterialGroups[ResolvedMaterialGroupIndex];
    }

    bool IsWorldBoundingBoxVisibleByFrustum(const DirectX::BoundingOrientedBox& WorldBoundingBox, const Game::Frustum* CullingFrustumComponent, bool IsFrustumCullingEnabled) {
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

    void AppendTerrainDrawRecord(const Game::TerrainTileMetadata& TileMetadata, const Game::TerrainRenderResource& Resource, const Game::ModelNode& Node, const Game::RegisteredMaterialGroup* ResolvedMaterialGroup, std::uint32_t ObjectIndex, std::uint32_t MaterialFlags, std::uint32_t PickFlags, const TerrainTileTessellationData& TessellationData, std::vector<Game::RFD::TerrainPatchContext>& OutTerrainPatchContexts, std::vector<Game::RFD::DrawRecord>& OutDrawRecords) {
        const std::uint32_t TileSubMeshIndex{ TileMetadata.mSubMeshIndex };
        const Game::ModelSubMesh& SubMesh{ Node.GetSubMesh(TileSubMeshIndex) };
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

        Game::RFD::DrawRecord DrawRecord{};
        DrawRecord.pso = Pipeline;
        DrawRecord.mesh = &Node;
        DrawRecord.submesh = TileSubMeshIndex;
        DrawRecord.pass = 0;
        DrawRecord.objectIndex = ObjectIndex;
        DrawRecord.materialIndex = ResolvedMaterialIndex;
        DrawRecord.flags = MaterialFlags | PickFlags;
        DrawRecord.TerrainPatchContextIndex = static_cast<std::uint32_t>(OutTerrainPatchContexts.size());
        DrawRecord.pad0 = 0;
        OutTerrainPatchContexts.push_back(BuildTerrainPatchContext(TileMetadata, Resource, TessellationData));
        OutDrawRecords.push_back(DrawRecord);
    }
}

namespace Game {
    TerrainRenderSystem::TerrainRenderSystem() {
    }

    TerrainRenderSystem::~TerrainRenderSystem() {
    }

    TerrainRenderSystem::TerrainRenderSystem(const TerrainRenderSystem& Other)
        : mName{ Other.mName } {
    }

    TerrainRenderSystem& TerrainRenderSystem::operator=(const TerrainRenderSystem& Other) {
        (void)Other;
        return *this;
    }

    TerrainRenderSystem::TerrainRenderSystem(TerrainRenderSystem&& Other) noexcept
        : mName{ std::move(Other.mName) } {
    }

    TerrainRenderSystem& TerrainRenderSystem::operator=(TerrainRenderSystem&& Other) noexcept {
        (void)Other;
        return *this;
    }

    const std::string& TerrainRenderSystem::Name() const {
        return mName;
    }

    Phase TerrainRenderSystem::GetPhase() const {
        return Phase::Render;
    }

    std::span<const ComponentAccess> TerrainRenderSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 7> Accesses{ { { typeid(Transform), Access::Read }, { typeid(TerrainRenderer), Access::Read }, { typeid(EntityHierarchy), Access::Read }, { typeid(Material), Access::Read }, { typeid(BoundingBox), Access::Write }, { typeid(Frustum), Access::Read }, { typeid(Culling), Access::Read } } };
        return Accesses;
    }

    std::span<const ResourceAccess> TerrainRenderSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 3> Accesses{ { { typeid(RFD::RenderFrameData), Access::Write }, { typeid(std::vector<RegisteredMaterialGroup>), Access::Read }, { typeid(Arche::EntityID), Access::Read } } };
        return Accesses;
    }

    void TerrainRenderSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        RFD::RenderFrameData& RenderData{ Ctx.RenderData };
        const std::vector<RegisteredMaterialGroup>& MaterialGroups{ *Ctx.MaterialGroups };
        const Frustum* CullingFrustumComponent{ nullptr };
        const std::uint32_t ShadowCascadeCount{ RFD::ResolveShadowCascadeCount(RenderData.shadowMapping) };
        const std::array<DirectX::BoundingOrientedBox, RFD::ShadowCascadeMaxCount> ShadowCullingBoxes{ RFD::BuildShadowCullingBoxes(RenderData.shadowMapping) };
        SimpleMath::Vector3 CameraPosition{};
        bool HasCameraPosition{ false };

        for (auto [CameraTransformComponent, CameraComponent, FrustumComponent] : World.Query<Transform, Camera, Frustum>()) {
            if (CameraComponent.isActive == false) {
                continue;
            }

            CullingFrustumComponent = &FrustumComponent;
            CameraPosition = CameraTransformComponent.position;
            HasCameraPosition = true;
            break;
        }

        for (auto [TransformComponent, Renderer, HierarchyComponent] : World.Query<Transform, TerrainRenderer, EntityHierarchy>()) {
            (void)TransformComponent;
            const Arche::EntityID EntityId{ HierarchyComponent.self };
            if (Renderer.mTileMetadataIndex != InvalidTerrainTileMetadataIndex || Renderer.mResource == nullptr || Renderer.mActive == false) {
                continue;
            }

            const std::shared_ptr<Model>& ModelData{ Renderer.mResource->GetModel() };
            if (ModelData == nullptr) {
                continue;
            }

            const ModelNode* NodePointer{ ModelData->GetRootNode() };
            if (NodePointer == nullptr || NodePointer->GetSubMeshes().empty() == true) {
                continue;
            }

            if (Renderer.mResource->GetHeightFieldSrvDescriptorIndex() == InvalidSrvDescriptorIndex) {
                continue;
            }

            SimpleMath::Matrix NodeWorld{};
            const bool IsWorldMatrixBuilt{ TryBuildWorldMatrix(World, EntityId, NodeWorld) };
            if (IsWorldMatrixBuilt == false) {
                continue;
            }

            const Culling* CullingComponent{ std::as_const(World).GetComponent<Culling>(EntityId) };
            const bool IsFrustumCullingEnabled{ CullingComponent == nullptr ? true : CullingComponent->frustumCulling };

            BoundingBox* BoundingBoxComponent{ World.GetComponent<BoundingBox>(EntityId) };
            DirectX::BoundingOrientedBox ParentWorldBoundingBox{};
            const bool IsParentVisible{ IsTileVisibleByFrustum(Renderer.mResource->GetLocalBoundingBox(), NodeWorld, CullingFrustumComponent, IsFrustumCullingEnabled, ParentWorldBoundingBox) };
            if (BoundingBoxComponent != nullptr) {
                BoundingBoxComponent->SetWorldObb(ParentWorldBoundingBox);
            }

            std::array<bool, RFD::ShadowCascadeMaxCount> IsParentVisibleByShadowCascade{};
            bool HasVisibleShadowParent{ false };
            for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1) {
                IsParentVisibleByShadowCascade[CascadeIndex] = IsWorldBoundingBoxVisibleByShadowBox(ParentWorldBoundingBox, ShadowCullingBoxes[CascadeIndex], IsFrustumCullingEnabled);
                if (IsParentVisibleByShadowCascade[CascadeIndex] == true) {
                    HasVisibleShadowParent = true;
                }
            }

            if (IsParentVisible == false && HasVisibleShadowParent == false) {
                continue;
            }

            if (IsParentVisible == true && BoundingBoxComponent != nullptr && BoundingBoxComponent->HasWorldObb() == true) {
                AppendBoundingBoxContext(BoundingBoxComponent->GetWorldObb(), RenderData);
            }

            const std::vector<TerrainTileMetadata>& TileMetadataItems{ Renderer.mResource->GetTileMetadata() };
            std::vector<TerrainTileTessellationData> TileTessellationItems{};
            TileTessellationItems.resize(TileMetadataItems.size());
            for (std::size_t TileMetadataIndex{ 0 }; TileMetadataIndex < TileMetadataItems.size(); ++TileMetadataIndex) {
                const TerrainTileMetadata& TileMetadata{ TileMetadataItems[TileMetadataIndex] };
                const std::uint32_t SelectedLodIndex{ SelectLodIndex(TileMetadata, NodeWorld, CameraPosition, HasCameraPosition, *Renderer.mResource) };
                const float TessFactor{ CalculateTerrainTessFactor(Renderer.mResource->GetTileQuadCount(), SelectedLodIndex) };
                TerrainTileTessellationData& TessellationData{ TileTessellationItems[TileMetadataIndex] };
                TessellationData.BaseTessFactor = TessFactor;
                TessellationData.OuterTessFactors[TerrainEdgeNegativeXIndex] = TessFactor;
                TessellationData.OuterTessFactors[TerrainEdgeNegativeZIndex] = TessFactor;
                TessellationData.OuterTessFactors[TerrainEdgePositiveXIndex] = TessFactor;
                TessellationData.OuterTessFactors[TerrainEdgePositiveZIndex] = TessFactor;
            }

            for (std::size_t TileMetadataIndex{ 0 }; TileMetadataIndex < TileMetadataItems.size(); ++TileMetadataIndex) {
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

            const Material* MaterialComponent{ std::as_const(World).GetComponent<Material>(EntityId) };
            const std::uint32_t MaterialFlags{ MaterialComponent == nullptr ? 0u : MaterialComponent->Flags };
            const TerrainRenderer* PickedTerrainRenderer{ std::as_const(World).GetComponent<TerrainRenderer>(Ctx.PickedEntityId) };
            const bool IsPickedParentHierarchy{ IsEntityWithinPickedHierarchy(World, EntityId, Ctx.PickedEntityId) };
            const bool IsPickedTileInThisTerrain{ PickedTerrainRenderer != nullptr && PickedTerrainRenderer->mResource == Renderer.mResource && PickedTerrainRenderer->mTileMetadataIndex != InvalidTerrainTileMetadataIndex && IsEntityWithinPickedHierarchy(World, Ctx.PickedEntityId, EntityId) };
            const std::uint32_t PickedTileMetadataIndex{ IsPickedTileInThisTerrain == true ? PickedTerrainRenderer->mTileMetadataIndex : InvalidTerrainTileMetadataIndex };
            const RegisteredMaterialGroup* ResolvedMaterialGroup{ ResolveMaterialGroup(MaterialGroups, MaterialComponent) };

            bool HasModelContext{ false };
            std::uint32_t ObjectIndex{ 0 };
            std::array<bool, RFD::ShadowCascadeMaxCount> HasShadowModelContexts{};
            std::array<std::uint32_t, RFD::ShadowCascadeMaxCount> ShadowObjectIndices{};
            for (std::size_t TileMetadataIndex{ 0 }; TileMetadataIndex < TileMetadataItems.size(); ++TileMetadataIndex) {
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
                        ModelContext.objectID = static_cast<std::uint32_t>(RenderData.modelContexts.size());
                        ObjectIndex = ModelContext.objectID;
                        RenderData.modelContexts.push_back(ModelContext);
                        HasModelContext = true;
                    }

                    AppendBoundingBoxContext(TileWorldBoundingBox, RenderData);
                    AppendTerrainDrawRecord(TileMetadata, *Renderer.mResource, *NodePointer, ResolvedMaterialGroup, ObjectIndex, MaterialFlags, IsPickedTile == true ? PickedDrawFlagBitMask : 0u, TileTessellationItems[TileMetadataIndex], RenderData.TerrainPatchContexts, RenderData.drawRecords);
                }

                for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1) {
                    if (IsParentVisibleByShadowCascade[CascadeIndex] == false) {
                        continue;
                    }

                    const bool IsVisibleByShadow{ IsWorldBoundingBoxVisibleByShadowBox(TileWorldBoundingBox, ShadowCullingBoxes[CascadeIndex], IsFrustumCullingEnabled) };
                    if (IsVisibleByShadow == false) {
                        continue;
                    }

                    RFD::ShadowRenderContext& ShadowRenderContext{ RenderData.ShadowRenderContexts[CascadeIndex] };
                    if (HasShadowModelContexts[CascadeIndex] == false) {
                        RFD::ModelContext ShadowModelContext{};
                        ShadowModelContext.world = NodeWorld;
                        ShadowModelContext.prevWorld = ShadowModelContext.world;
                        ShadowModelContext.objectID = static_cast<std::uint32_t>(ShadowRenderContext.ModelContexts.size());
                        ShadowObjectIndices[CascadeIndex] = ShadowModelContext.objectID;
                        ShadowRenderContext.ModelContexts.push_back(ShadowModelContext);
                        HasShadowModelContexts[CascadeIndex] = true;
                    }

                    AppendTerrainDrawRecord(TileMetadata, *Renderer.mResource, *NodePointer, ResolvedMaterialGroup, ShadowObjectIndices[CascadeIndex], MaterialFlags, 0u, TileTessellationItems[TileMetadataIndex], ShadowRenderContext.TerrainPatchContexts, ShadowRenderContext.DrawRecords);
                }
            }
        }
    }

    std::uint32_t TerrainRenderSystem::SelectLodIndex(const TerrainTileMetadata& TileMetadata, const SimpleMath::Matrix& WorldMatrix, const SimpleMath::Vector3& CameraPosition, bool HasCameraPosition, const TerrainRenderResource& Resource) const {
        const std::size_t AvailableLodCount{ static_cast<std::size_t>(Resource.GetLodCount()) };
        if (AvailableLodCount <= 1ULL || HasCameraPosition == false) {
            return 0u;
        }

        const std::vector<float>& LodDistances{ Resource.GetLodDistances() };
        if (LodDistances.empty() == true) {
            return 0u;
        }

        const SimpleMath::Vector3 WorldCenter{ SimpleMath::Vector3::Transform(TileMetadata.mCenter, WorldMatrix) };
        const SimpleMath::Vector3 CenterToCamera{ WorldCenter - CameraPosition };
        const float DistanceSquared{ CenterToCamera.LengthSquared() };
        std::uint32_t SelectedLodIndex{ 0 };
        const std::size_t ThresholdCount{ (std::min)(LodDistances.size(), AvailableLodCount - 1ULL) };

        for (std::size_t ThresholdIndex{ 0 }; ThresholdIndex < ThresholdCount; ++ThresholdIndex) {
            const float ThresholdDistance{ LodDistances[ThresholdIndex] };
            if (ThresholdDistance > 0.0f && DistanceSquared < ThresholdDistance * ThresholdDistance) {
                return static_cast<std::uint32_t>(ThresholdIndex);
            }

            SelectedLodIndex = static_cast<std::uint32_t>(ThresholdIndex + 1ULL);
        }

        return SelectedLodIndex;
    }

    bool TerrainRenderSystem::IsTileVisibleByFrustum(const DirectX::BoundingOrientedBox& LocalBoundingBox, const SimpleMath::Matrix& WorldMatrix, const Frustum* CullingFrustumComponent, bool IsFrustumCullingEnabled, DirectX::BoundingOrientedBox& OutWorldBoundingBox) const {
        LocalBoundingBox.Transform(OutWorldBoundingBox, WorldMatrix);

        if (IsFrustumCullingEnabled == false) {
            return true;
        }

        if (CullingFrustumComponent == nullptr) {
            return true;
        }

        return CullingFrustumComponent->Intersects(OutWorldBoundingBox);
    }
}
