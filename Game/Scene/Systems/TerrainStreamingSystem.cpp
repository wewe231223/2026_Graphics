#include "TerrainStreamingSystem.h"

#include <array>
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

    PhysicsTerrainActor::ActorDesc BuildStreamingTerrainActorDesc(const Game::TerrainRenderResource& Resource, const Game::Transform& TransformComponent) {
        const Game::TerrainBuildDesc& BuildDesc{ Resource.GetBuildDesc() };
        const Game::HeightFieldData& HeightField{ Resource.GetHeightFieldData() };
        PhysicsTerrainActor::ActorDesc Desc{ PhysicsTerrainActor::BuildHeightFieldActorDesc(HeightField.Width, HeightField.Height, HeightField.HeightValues, BuildDesc.MaxHeight, BuildDesc.CellSizeX, BuildDesc.CellSizeZ, BuildDesc.CenterOrigin) };
        Desc.Position = TransformComponent.position;
        Desc.Rotation = TransformComponent.rotationEuler;
        Desc.Scale = TransformComponent.scale;
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

    void UpdateTerrainPhysicsActor(Game::TerrainRenderResource& Resource, const Game::Transform& TransformComponent, const Game::PhysicsActor* PhysicsActorComponent, IPhysicsWorld* PhysicsWorldResource) {
        if (PhysicsActorComponent == nullptr || PhysicsWorldResource == nullptr) {
            return;
        }

        PhysicsTerrainActor* TerrainActor{ PhysicsWorldResource->GetTerrainActor(PhysicsActorComponent->mActorIndex) };
        if (TerrainActor == nullptr) {
            return;
        }

        const PhysicsTerrainActor::ActorDesc Desc{ BuildStreamingTerrainActorDesc(Resource, TransformComponent) };
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
        static std::array<ResourceAccess, 2> Accesses{ { { typeid(AssetRegistry), Access::Write }, { typeid(IPhysicsWorld), Access::Write } } };
        return Accesses;
    }

    void TerrainStreamingSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        SimpleMath::Vector3 FocusPosition{};
        const bool HasFocusPosition{ TryResolveStreamingFocusPosition(World, FocusPosition) };
        if (HasFocusPosition == false || Ctx.AssetRegistryResource == nullptr) {
            return;
        }

        for (auto [TransformComponent, Renderer, HierarchyComponent] : World.Query<Transform, TerrainRenderer, EntityHierarchy>()) {
            if (Renderer.mTileMetadataIndex != InvalidTerrainTileMetadataIndex || Renderer.mResource == nullptr || Renderer.mActive == false || Renderer.mResource->IsStreamingEnabled() == false) {
                continue;
            }

            TerrainRenderResource& Resource{ *Renderer.mResource };
            const bool IsStreamUpdated{ Ctx.AssetRegistryResource->UpdateTerrainStreaming(Resource, FocusPosition, Ctx.RenderData.globals.frameIndex) };
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
            UpdateTerrainPhysicsActor(Resource, TransformComponent, PhysicsActorComponent, Ctx.PhysicsWorldResource);
        }
    }
}
