#include "PhysicsActorUpdateSystem.h"

#include <array>
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/PhysicsActor.h"
#include "Game/Scene/Components/Transform.h"

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
        static std::array<ResourceAccess, 1> Accesses{ { { typeid(IPhysicsWorld), Access::Read } } };
        return Accesses;
    }

    void PhysicsActorUpdateSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        const IPhysicsWorld* PhysicsWorldResource{ Ctx.PhysicsWorldResource };

        for (auto [PhysicsActorComponent, TransformComponent, EntityHierarchyComponent] : World.Query<PhysicsActor, Transform, EntityHierarchy>()) {
            PhysicsActorBase* ActorPointer{ PhysicsActorComponent.mActorPointer };
            if (ActorPointer == nullptr) {
                if (PhysicsActorComponent.mHasCachedSnapshot == false) {
                    continue;
                }

                TransformComponent.position = PhysicsActorComponent.mCachedPosition;
                TransformComponent.rotation = PhysicsActorComponent.mCachedOrientation;
                TransformComponent.scale = PhysicsActorComponent.mCachedScale;
                TransformComponent.UpdateEulerRadiansFromRotation();

                BoundingBox* BoundingBoxComponent{ World.GetComponent<BoundingBox>(EntityHierarchyComponent.self) };
                if (BoundingBoxComponent != nullptr) {
                    BoundingBoxComponent->SetWorldObb(PhysicsActorComponent.mCachedWorldBoundingBox);
                }

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
