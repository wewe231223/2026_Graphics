#include "TerrainRenderSystem.h"

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

        for (auto [CameraComponent, FrustumComponent] : World.Query<Camera, Frustum>()) {
            if (CameraComponent.isActive == false) {
                continue;
            }

            CullingFrustumComponent = &FrustumComponent;
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

            if (IsParentVisible == false) {
                continue;
            }

            if (BoundingBoxComponent != nullptr && BoundingBoxComponent->HasWorldObb() == true) {
                AppendBoundingBoxContext(BoundingBoxComponent->GetWorldObb(), RenderData);
            }

            const std::vector<TerrainTileMetadata>& TileMetadataItems{ Renderer.mResource->GetTileMetadata() };

            const Material* MaterialComponent{ std::as_const(World).GetComponent<Material>(EntityId) };
            const std::uint32_t MaterialFlags{ MaterialComponent == nullptr ? 0u : MaterialComponent->Flags };
            const TerrainRenderer* PickedTerrainRenderer{ std::as_const(World).GetComponent<TerrainRenderer>(Ctx.PickedEntityId) };
            const bool IsPickedParentHierarchy{ IsEntityWithinPickedHierarchy(World, EntityId, Ctx.PickedEntityId) };
            const bool IsPickedTileInThisTerrain{ PickedTerrainRenderer != nullptr && PickedTerrainRenderer->mResource == Renderer.mResource && PickedTerrainRenderer->mTileMetadataIndex != InvalidTerrainTileMetadataIndex && IsEntityWithinPickedHierarchy(World, Ctx.PickedEntityId, EntityId) };
            const std::uint32_t PickedTileMetadataIndex{ IsPickedTileInThisTerrain == true ? PickedTerrainRenderer->mTileMetadataIndex : InvalidTerrainTileMetadataIndex };
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

            bool HasModelContext{ false };
            std::uint32_t ObjectIndex{ 0 };
            for (std::size_t TileMetadataIndex{ 0 }; TileMetadataIndex < TileMetadataItems.size(); ++TileMetadataIndex) {
                const TerrainTileMetadata& TileMetadata{ TileMetadataItems[TileMetadataIndex] };
                if (TileMetadata.mSubMeshIndexByLod.empty() == true) {
                    continue;
                }

                const std::uint32_t LodSubMeshIndex{ TileMetadata.mSubMeshIndexByLod[0] };
                if (LodSubMeshIndex >= NodePointer->GetSubMeshes().size()) {
                    continue;
                }

                DirectX::BoundingOrientedBox TileWorldBoundingBox{};
                const bool IsVisible{ IsTileVisibleByFrustum(TileMetadata.mLocalBoundingBox, NodeWorld, CullingFrustumComponent, IsFrustumCullingEnabled, TileWorldBoundingBox) };
                if (IsVisible == false) {
                    continue;
                }

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

                const ModelSubMesh& SubMesh{ NodePointer->GetSubMesh(LodSubMeshIndex) };
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
                DrawRecord.mesh = NodePointer;
                DrawRecord.submesh = LodSubMeshIndex;
                DrawRecord.pass = 0;
                DrawRecord.objectIndex = ObjectIndex;
                DrawRecord.materialIndex = ResolvedMaterialIndex;
                const bool IsPickedTile{ IsPickedParentHierarchy == true || PickedTileMetadataIndex == static_cast<std::uint32_t>(TileMetadataIndex) };
                DrawRecord.flags = MaterialFlags | (IsPickedTile == true ? PickedDrawFlagBitMask : 0u);
                DrawRecord.pad0 = 0;
                RenderData.drawRecords.push_back(DrawRecord);
            }
        }
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
