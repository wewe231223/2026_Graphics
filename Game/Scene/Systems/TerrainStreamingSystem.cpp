#include "TerrainStreamingSystem.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "Game/Model/AssetRegistry.h"
#include "Game/Model/TerrainRenderResource.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/PhysicsActor.h"
#include "Game/Scene/Components/TerrainRenderer.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    bool TryResolveStreamingFocusPosition(Arche::World& World, SimpleMath::Vector3& OutFocusPosition) {
        for (auto [TransformComponent, CameraComponent] : World.Query<Game::Transform, Game::Camera>()) {
            if (CameraComponent.isActive == false) {
                continue;
            }

            OutFocusPosition = TransformComponent.position;
            return true;
        }

        return false;
    }

    PhysicsTerrainActor::ActorDesc BuildStreamingTerrainActorDesc(const Game::TerrainRenderResource& Resource, const Game::Transform& TransformComponent, std::uint32_t TerrainId, Game::TerrainManager& TerrainManagerInstance) {
        const Game::TerrainBuildDesc& BuildDesc{ Resource.GetBuildDesc() };
        const std::shared_ptr<const Game::HeightFieldData>& HeightField{ Resource.GetHeightFieldDataPointer() };
        PhysicsTerrainActor::ActorDesc Desc{};
        if (HeightField == nullptr || HeightField->HeightValues.empty() == true) {
            return Desc;
        }

        Desc.HeightFieldWidth = HeightField->Width;
        Desc.HeightFieldHeight = HeightField->Height;
        Desc.HeightFieldCellSizeX = BuildDesc.CellSizeX;
        Desc.HeightFieldCellSizeZ = BuildDesc.CellSizeZ;
        Desc.HeightFieldMaxHeight = BuildDesc.MaxHeight;
        Desc.HeightFieldCenterOrigin = BuildDesc.CenterOrigin;
        Desc.HeightFieldValues = std::shared_ptr<const std::vector<float>>{ HeightField, &HeightField->HeightValues };
        Desc.HalfExtentX = HeightField->Width > 1U ? static_cast<float>(HeightField->Width - 1U) * BuildDesc.CellSizeX * 0.5F : 0.0F;
        Desc.HalfExtentZ = HeightField->Height > 1U ? static_cast<float>(HeightField->Height - 1U) * BuildDesc.CellSizeZ * 0.5F : 0.0F;
        Desc.Position = TransformComponent.position;
        Desc.Rotation = TransformComponent.rotationEuler;
        Desc.Scale = TransformComponent.scale;
        Desc.mTerrainWorldData = std::make_shared<const Game::TerrainWorldData>(PhysicsTerrainActor::BuildTerrainWorldDataFromActorDesc(Desc, TerrainId));
        const Game::TerrainDataHandle TerrainHandle{ TerrainManagerInstance.UpsertTerrainData(*Desc.mTerrainWorldData) };
        std::shared_ptr<const Game::TerrainWorldData> ManagedTerrainWorldData{};
        if (TerrainManagerInstance.TryGetTerrainWorldData(TerrainHandle, ManagedTerrainWorldData) == true) {
            Desc.mTerrainHandle = TerrainHandle;
            Desc.mTerrainQuery = &TerrainManagerInstance;
            Desc.mTerrainWorldData = ManagedTerrainWorldData;
        }

        return Desc;
    }

    void UpdateTerrainBoundingBoxes(Arche::World& World, Game::TerrainRenderResource& Resource, Game::BoundingBox* ParentBoundingBox) {
        if (ParentBoundingBox != nullptr) {
            ParentBoundingBox->SetObb(Resource.GetLocalBoundingBox());
        }

        const std::vector<Game::TerrainTileMetadata>& TileMetadataItems{ Resource.GetTileMetadata() };
        for (auto [Renderer, BoundingBoxComponent] : World.Query<Game::TerrainRenderer, Game::BoundingBox>()) {
            if (Renderer.mResource != &Resource || Renderer.mTileMetadataIndex == Game::InvalidTerrainTileMetadataIndex) {
                continue;
            }

            const std::uint32_t TileMetadataIndex{ Renderer.mTileMetadataIndex };
            if (TileMetadataIndex >= TileMetadataItems.size()) {
                continue;
            }

            BoundingBoxComponent.SetObb(TileMetadataItems[TileMetadataIndex].mLocalBoundingBox);
        }
    }

    void UpdateTerrainPhysicsActor(Game::TerrainRenderResource& Resource, const Game::Transform& TransformComponent, const Game::PhysicsActor* PhysicsActorComponent, IPhysicsWorld* PhysicsWorldResource, Game::TerrainManager* TerrainManagerResource) {
        if (PhysicsActorComponent == nullptr || PhysicsWorldResource == nullptr || TerrainManagerResource == nullptr) {
            return;
        }

        const std::uint32_t PhysicsActorId{ Game::ResolvePhysicsActorId(*PhysicsActorComponent) };
        PhysicsTerrainActor* TerrainActor{ PhysicsWorldResource->GetTerrainActor(PhysicsActorId) };
        if (TerrainActor == nullptr) {
            return;
        }

        const PhysicsTerrainActor::ActorDesc Desc{ BuildStreamingTerrainActorDesc(Resource, TransformComponent, PhysicsActorId, *TerrainManagerResource) };
        TerrainActor->SetActorDesc(Desc);
    }
}

namespace Game {
    TerrainStreamingSystem::TerrainStreamingSystem() {
    }

    TerrainStreamingSystem::~TerrainStreamingSystem() {
    }

    TerrainStreamingSystem::TerrainStreamingSystem(const TerrainStreamingSystem& Other)
        : mName{ Other.mName } {
    }

    TerrainStreamingSystem& TerrainStreamingSystem::operator=(const TerrainStreamingSystem& Other) {
        (void)Other;
        return *this;
    }

    TerrainStreamingSystem::TerrainStreamingSystem(TerrainStreamingSystem&& Other) noexcept
        : mName{ std::move(Other.mName) } {
    }

    TerrainStreamingSystem& TerrainStreamingSystem::operator=(TerrainStreamingSystem&& Other) noexcept {
        (void)Other;
        return *this;
    }

    const std::string& TerrainStreamingSystem::Name() const {
        return mName;
    }

    Phase TerrainStreamingSystem::GetPhase() const {
        return Phase::PhysicsActorUpdate;
    }

    std::span<const ComponentAccess> TerrainStreamingSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 6> Accesses{ { { typeid(Transform), Access::Write }, { typeid(TerrainRenderer), Access::Read }, { typeid(EntityHierarchy), Access::Read }, { typeid(Camera), Access::Read }, { typeid(BoundingBox), Access::Write }, { typeid(PhysicsActor), Access::Read } } };
        return Accesses;
    }

    std::span<const ResourceAccess> TerrainStreamingSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 3> Accesses{ { { typeid(AssetRegistry), Access::Write }, { typeid(IPhysicsWorld), Access::Write }, { typeid(TerrainManager), Access::Write } } };
        return Accesses;
    }

    void TerrainStreamingSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        SimpleMath::Vector3 FocusPosition{};
        const bool HasFocusPosition{ TryResolveStreamingFocusPosition(World, FocusPosition) };
        if (HasFocusPosition == false || Ctx.AssetRegistryResource == nullptr || Ctx.TerrainManagerResource == nullptr) {
            return;
        }

        for (auto [TransformComponent, Renderer, HierarchyComponent] : World.Query<Transform, TerrainRenderer, EntityHierarchy>()) {
            if (Renderer.mTileMetadataIndex != InvalidTerrainTileMetadataIndex || Renderer.mResource == nullptr || Renderer.mActive == false || Renderer.mResource->IsStreamingEnabled() == false) {
                continue;
            }

            TerrainRenderResource& Resource{ *Renderer.mResource };
            const bool IsStreamUpdated{ Ctx.AssetRegistryResource->UpdateTerrainStreaming(Resource, *Ctx.TerrainManagerResource, FocusPosition, Ctx.RenderData.mFrameGlobals.mFrameIndex) };
            const SimpleMath::Vector3 PreviousPosition{ TransformComponent.position };
            TransformComponent.position.x = Resource.GetStreamWorldOriginX();
            TransformComponent.position.z = Resource.GetStreamWorldOriginZ();
            const bool IsTransformChanged{ PreviousPosition != TransformComponent.position };

            if (IsStreamUpdated == false && IsTransformChanged == false) {
                continue;
            }

            BoundingBox* ParentBoundingBox{ World.GetComponent<BoundingBox>(HierarchyComponent.self) };
            UpdateTerrainBoundingBoxes(World, Resource, ParentBoundingBox);

            const PhysicsActor* PhysicsActorComponent{ std::as_const(World).GetComponent<PhysicsActor>(HierarchyComponent.self) };
            UpdateTerrainPhysicsActor(Resource, TransformComponent, PhysicsActorComponent, Ctx.PhysicsWorldResource, Ctx.TerrainManagerResource);
        }
    }
}
