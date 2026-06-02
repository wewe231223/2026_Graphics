#include "PhysicsActorUpdateSystem.h"

#include <array>
#include <cstddef>
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/PhysicsActor.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    void ApplyCachedPhysicsActorSnapshot(Arche::World& World, Game::PhysicsActor& PhysicsActorComponent, Game::Transform& TransformComponent, const Game::EntityHierarchy& EntityHierarchyComponent) {
        if (PhysicsActorComponent.mHasCachedSnapshot == false) {
            return;
        }

        TransformComponent.position = PhysicsActorComponent.mCachedPosition;
        TransformComponent.rotation = PhysicsActorComponent.mCachedOrientation;
        TransformComponent.scale = PhysicsActorComponent.mCachedScale;
        TransformComponent.UpdateEulerRadiansFromRotation();

        Game::BoundingBox* BoundingBoxComponent{ World.GetComponent<Game::BoundingBox>(EntityHierarchyComponent.self) };
        if (BoundingBoxComponent != nullptr) {
            BoundingBoxComponent->SetWorldObb(PhysicsActorComponent.mCachedWorldBoundingBox);
        }
    }

    const PhysicsActorSnapshot* TryResolvePhysicsActorSnapshot(const PhysicsSnapshot& Snapshot, const Game::PhysicsActor& PhysicsActorComponent) {
        const std::uint32_t PhysicsActorId{ Game::ResolvePhysicsActorId(PhysicsActorComponent) };
        if (PhysicsActorId == Game::InvalidPhysicsActorId) {
            return nullptr;
        }

        const std::size_t SnapshotActorIndex{ static_cast<std::size_t>(PhysicsActorId) };
        if (SnapshotActorIndex >= Snapshot.mActorCount || SnapshotActorIndex >= Snapshot.mActors.size()) {
            return nullptr;
        }

        const PhysicsActorSnapshot& SnapshotActor{ Snapshot.mActors[SnapshotActorIndex] };
        if (SnapshotActor.mActorId != static_cast<ActorId>(PhysicsActorId)) {
            return nullptr;
        }

        return &SnapshotActor;
    }
}

namespace Game {
    const std::string& PhysicsActorUpdateSystem::Name() const {
        return mName;
    }

    Phase PhysicsActorUpdateSystem::GetPhase() const {
        return Phase::PhysicsActorUpdate;
    }

    std::span<const ComponentAccess> PhysicsActorUpdateSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 4> Accesses{ { { typeid(PhysicsActor), Access::Write }, { typeid(Transform), Access::Write }, { typeid(EntityHierarchy), Access::Read }, { typeid(BoundingBox), Access::Write } } };
        return Accesses;
    }

    std::span<const ResourceAccess> PhysicsActorUpdateSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 2> Accesses{ { { typeid(IPhysicsWorld), Access::Read }, { typeid(PhysicsSnapshot), Access::Read } } };
        return Accesses;
    }

    void PhysicsActorUpdateSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        if (Ctx.IsPhysicsRuntimeModeEnabled == true) {
            const PhysicsSnapshot* PhysicsSnapshotResource{ Ctx.PhysicsSnapshotResource };
            for (auto [PhysicsActorComponent, TransformComponent, EntityHierarchyComponent] : World.Query<PhysicsActor, Transform, EntityHierarchy>()) {
                if (PhysicsSnapshotResource == nullptr) {
                    ApplyCachedPhysicsActorSnapshot(World, PhysicsActorComponent, TransformComponent, EntityHierarchyComponent);
                    continue;
                }

                const PhysicsActorSnapshot* SnapshotActor{ TryResolvePhysicsActorSnapshot(*PhysicsSnapshotResource, PhysicsActorComponent) };
                if (SnapshotActor == nullptr) {
                    ApplyCachedPhysicsActorSnapshot(World, PhysicsActorComponent, TransformComponent, EntityHierarchyComponent);
                    continue;
                }

                PhysicsActorComponent.mActorType = SnapshotActor->mActorType;
                UpdatePhysicsActorCachedSnapshot(PhysicsActorComponent, SnapshotActor->mPosition, SnapshotActor->mOrientation, SnapshotActor->mScale, SnapshotActor->mVelocity, SnapshotActor->mWorldBoundingBox);

                TransformComponent.position = SnapshotActor->mPosition;
                TransformComponent.rotation = SnapshotActor->mOrientation;
                TransformComponent.scale = SnapshotActor->mScale;
                TransformComponent.UpdateEulerRadiansFromRotation();

                BoundingBox* BoundingBoxComponent{ World.GetComponent<BoundingBox>(EntityHierarchyComponent.self) };
                if (BoundingBoxComponent != nullptr) {
                    BoundingBoxComponent->SetWorldObb(SnapshotActor->mWorldBoundingBox);
                }
            }

            return;
        }

        const IPhysicsWorld* PhysicsWorldResource{ Ctx.PhysicsWorldResource };

        for (auto [PhysicsActorComponent, TransformComponent, EntityHierarchyComponent] : World.Query<PhysicsActor, Transform, EntityHierarchy>()) {
            PhysicsActorBase* ActorPointer{ PhysicsActorComponent.mActorPointer };
            if (ActorPointer == nullptr) {
                ApplyCachedPhysicsActorSnapshot(World, PhysicsActorComponent, TransformComponent, EntityHierarchyComponent);
                continue;
            }

            DirectX::SimpleMath::Vector3 PhysicsPosition{ ActorPointer->GetPosition() };
            DirectX::SimpleMath::Quaternion PhysicsOrientation{ ActorPointer->GetOrientation() };
            DirectX::SimpleMath::Vector3 PhysicsScale{ ActorPointer->GetScale() };
            if (ActorPointer->GetActorType() == PhysicsActorBase::PhysicsActorType::Dynamic && PhysicsWorldResource != nullptr) {
                static_cast<void>(PhysicsWorldResource->TryGetInterpolatedActorTransform(*ActorPointer, PhysicsPosition, PhysicsOrientation, PhysicsScale));
            }

            const DirectX::BoundingOrientedBox& PhysicsWorldBoundingBox{ ActorPointer->GetWorldBoundingBox() };
            UpdatePhysicsActorCachedSnapshot(PhysicsActorComponent, PhysicsPosition, PhysicsOrientation, PhysicsScale, ActorPointer->GetVelocity(), PhysicsWorldBoundingBox);

            TransformComponent.position = PhysicsPosition;
            TransformComponent.rotation = PhysicsOrientation;
            TransformComponent.scale = PhysicsScale;
            TransformComponent.UpdateEulerRadiansFromRotation();

            BoundingBox* BoundingBoxComponent{ World.GetComponent<BoundingBox>(EntityHierarchyComponent.self) };
            if (BoundingBoxComponent != nullptr) {
                BoundingBoxComponent->SetWorldObb(PhysicsWorldBoundingBox);
            }
        }
    }
}
