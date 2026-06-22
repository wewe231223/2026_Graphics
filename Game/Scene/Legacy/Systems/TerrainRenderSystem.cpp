#include "TerrainRenderSystem.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
#include "RenderContract/Gather/RenderGatherResultMerger.h"
#include "RenderContract/Writer/TerrainRenderWriter.h"
#include "Terrain/TerrainRenderDataBuilder.h"

namespace {
    const Game::RegisteredMaterialGroup* ResolveMaterialGroup(const std::vector<Game::RegisteredMaterialGroup>* MaterialGroups, const Game::Material* MaterialComponent) {
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

    bool IsEntityWithinPickedHierarchy(Arche::World& World, Arche::EntityID EntityId, Arche::EntityID PickedEntityId) {
        if (PickedEntityId == Arche::NullEntityID) {
            return false;
        }

        Arche::EntityID CurrentEntityId{ EntityId };
        while (CurrentEntityId != Arche::NullEntityID) {
            if (CurrentEntityId == PickedEntityId) {
                return true;
            }

            const Game::EntityHierarchy* HierarchyComponent{ std::as_const(World).GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (HierarchyComponent == nullptr) {
                break;
            }

            CurrentEntityId = HierarchyComponent->parent;
        }

        return false;
    }

    std::vector<Terrain::TerrainRenderSubMeshBinding> BuildTerrainRenderSubMeshBindings(const Game::ModelNode& Node, const Game::RegisteredMaterialGroup* MaterialGroup) {
        std::vector<Terrain::TerrainRenderSubMeshBinding> Bindings{};
        Bindings.resize(Node.GetSubMeshes().size());
        if (MaterialGroup == nullptr) {
            return Bindings;
        }

        for (std::size_t SubMeshIndex{}; SubMeshIndex < Node.GetSubMeshes().size(); SubMeshIndex += 1U) {
            const Game::ModelSubMesh& SubMesh{ Node.GetSubMesh(SubMeshIndex) };
            std::size_t MaterialItemIndex{ SubMesh.mMaterialGroupItemIndex };
            if (MaterialItemIndex >= MaterialGroup->Items.size()) {
                MaterialItemIndex = 0U;
            }

            if (MaterialItemIndex >= MaterialGroup->Items.size()) {
                continue;
            }

            const Game::RegisteredMaterialGroupItem& MaterialItem{ MaterialGroup->Items[MaterialItemIndex] };
            Terrain::TerrainRenderSubMeshBinding& Binding{ Bindings[SubMeshIndex] };
            Binding.mPipeline = MaterialItem.Pipeline;
            Binding.mMaterialIndex = MaterialItem.MaterialIndex;
        }

        return Bindings;
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
        static std::array<ResourceAccess, 3> Accesses{ { { typeid(RenderContract::RenderFrameData), Access::Write }, { typeid(std::vector<RegisteredMaterialGroup>), Access::Read }, { typeid(Arche::EntityID), Access::Read } } };
        return Accesses;
    }

    void TerrainRenderSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        RenderContract::RenderFrameData& RenderData{ Ctx.RenderData };
        RenderContract::RenderGatherResult GatherResult{};
        RenderContract::TerrainRenderWriter TerrainWriter{ GatherResult };
        const std::uint32_t FrameIndex{ RenderData.mFrameGlobals.mFrameIndex };
        const Frustum* CullingFrustumComponent{ nullptr };
        const std::uint32_t ShadowCascadeCount{ RenderContract::ResolveShadowCascadeCount(RenderData.mShadowMappingParameter) };
        const std::array<DirectX::BoundingOrientedBox, RenderContract::ShadowCascadeMaxCount> ShadowCullingBoxes{ RenderContract::BuildShadowCullingBoxes(RenderData.mShadowMappingParameter) };
        SimpleMath::Vector3 CameraPosition{};
        bool HasCameraPosition{};

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

            const Material* MaterialComponent{ std::as_const(World).GetComponent<Material>(EntityId) };
            const RegisteredMaterialGroup* MaterialGroup{ ResolveMaterialGroup(Ctx.MaterialGroups, MaterialComponent) };
            const std::vector<Terrain::TerrainRenderSubMeshBinding> SubMeshBindings{ BuildTerrainRenderSubMeshBindings(*NodePointer, MaterialGroup) };
            const Culling* CullingComponent{ std::as_const(World).GetComponent<Culling>(EntityId) };
            const TerrainRenderer* PickedTerrainRenderer{ std::as_const(World).GetComponent<TerrainRenderer>(Ctx.PickedEntityId) };
            const bool IsPickedTileInThisTerrain{ PickedTerrainRenderer != nullptr && PickedTerrainRenderer->mResource == Renderer.mResource && PickedTerrainRenderer->mTileMetadataIndex != InvalidTerrainTileMetadataIndex && IsEntityWithinPickedHierarchy(World, Ctx.PickedEntityId, EntityId) };

            Terrain::TerrainRenderInput Input{};
            Input.mTileMetadataItems = &Renderer.mResource->GetTileMetadata();
            Input.mLodDistances = &Renderer.mResource->GetLodDistances();
            Input.mMesh = NodePointer;
            Input.mSubMeshBindings = &SubMeshBindings;
            Input.mTerrainUploadFuture = Renderer.mResource->GetFrameUploadFuture(FrameIndex);
            Input.mLocalBoundingBox = Renderer.mResource->GetLocalBoundingBox();
            Input.mWorld = TransformComponent.worldMatrix;
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
            Input.mSplatMapSrvDescriptorIndex = Renderer.mResource->GetSplatMapSrvDescriptorIndex(FrameIndex);
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
            Input.mIsFrustumCullingEnabled = CullingComponent == nullptr || CullingComponent->frustumCulling;
            Input.mIsDrawBoundingBoxesEnabled = (RenderData.mFrameGlobals.mFlags & RenderContract::FrameGlobalFlagDrawBoundingBoxes) != 0U;
            Input.mIsPickedParentHierarchy = IsEntityWithinPickedHierarchy(World, EntityId, Ctx.PickedEntityId);
            Input.mHeightFieldFlipV = Renderer.mResource->IsHeightFieldFlipV();

            const Terrain::TerrainRenderResult Result{ Terrain::WriteTerrainRenderData(Input, TerrainWriter) };
            BoundingBox* BoundingBoxComponent{ World.GetComponent<BoundingBox>(EntityId) };
            if (BoundingBoxComponent != nullptr && Result.mHasParentWorldBoundingBox == true) {
                BoundingBoxComponent->SetWorldObb(Result.mParentWorldBoundingBox);
            }
        }

        const std::array<RenderContract::RenderGatherResult, 1> GatherResults{ std::move(GatherResult) };
        RenderContract::RenderGatherResultMerger::Merge(GatherResults, RenderData);
    }
}
