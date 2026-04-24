#include <algorithm>
#include <utility>

#include "PhysicsLib/Actors/Integrater/PhysicsKinematicIntegrater.h"
#include "PhysicsLib/Actors/PhysicsActorBase.h"
#include "PhysicsLib/Actors/PhysicsTerrainActor.h"
#include "PhysicsLib/Simulation/Mediator/IPhysicsWorldMediator.h"

#undef max
#undef min

namespace {
bool TryGetHighestTerrainSurfaceHeight(const IPhysicsActorRepository& ActorRepository, float WorldX, float WorldZ, float& OutSurfaceHeight) {
    std::vector<const PhysicsStaticActor*> StaticActors{ ActorRepository.CollectStaticActors() };
    std::size_t StaticActorCount{ StaticActors.size() };
    bool HasSurfaceHeight{};
    float HighestSurfaceHeight{};

    for (std::size_t StaticActorIndex{ 0U }; StaticActorIndex < StaticActorCount; ++StaticActorIndex) {
        const PhysicsStaticActor* StaticActor{ StaticActors[StaticActorIndex] };
        if (StaticActor == nullptr) {
            continue;
        }

        const PhysicsTerrainActor* TerrainActor{ dynamic_cast<const PhysicsTerrainActor*>(StaticActor) };
        if (TerrainActor == nullptr) {
            continue;
        }

        float SurfaceHeight{};
        bool HasCurrentSurfaceHeight{ TerrainActor->TryGetSurfaceHeightAtWorldPosition(WorldX, WorldZ, SurfaceHeight) };
        if (!HasCurrentSurfaceHeight) {
            continue;
        }

        if (!HasSurfaceHeight || SurfaceHeight > HighestSurfaceHeight) {
            HighestSurfaceHeight = SurfaceHeight;
            HasSurfaceHeight = true;
        }
    }

    if (!HasSurfaceHeight) {
        return false;
    }

    OutSurfaceHeight = HighestSurfaceHeight;
    return true;
}

float GetActorHalfHeight(const PhysicsActorBase& Actor) {
    const DirectX::BoundingOrientedBox& WorldBoundingBox{ Actor.GetWorldBoundingBox() };
    float HalfHeight{ std::max(WorldBoundingBox.Extents.y, 0.0F) };
    return HalfHeight;
}
}

PhysicsKinematicIntegrater::PhysicsKinematicIntegrater() {
}

PhysicsKinematicIntegrater::~PhysicsKinematicIntegrater() {
}

PhysicsKinematicIntegrater::PhysicsKinematicIntegrater(const PhysicsKinematicIntegrater& Other)
    : IPhysicsIntegrater{ Other } {
}

PhysicsKinematicIntegrater& PhysicsKinematicIntegrater::operator=(const PhysicsKinematicIntegrater& Other) {
    if (this == &Other) {
        return *this;
    }

    IPhysicsIntegrater::operator=(Other);
    return *this;
}

PhysicsKinematicIntegrater::PhysicsKinematicIntegrater(PhysicsKinematicIntegrater&& Other) noexcept
    : IPhysicsIntegrater{ std::move(Other) } {
}

PhysicsKinematicIntegrater& PhysicsKinematicIntegrater::operator=(PhysicsKinematicIntegrater&& Other) noexcept {
    if (this == &Other) {
        return *this;
    }

    IPhysicsIntegrater::operator=(std::move(Other));
    return *this;
}

void PhysicsKinematicIntegrater::Integrate(IPhysicsWorldMediator& WorldMediator, PhysicsActorBase& Actor, float DeltaTime) const {
    (void)WorldMediator;

    if (Actor.GetActorType() != PhysicsActorBase::PhysicsActorType::Kinematic) {
        return;
    }

    if (!Actor.GetIsActive() || DeltaTime <= 0.0F) {
        return;
    }

    DirectX::SimpleMath::Vector3 NextVelocity{ Actor.GetVelocity() };
    NextVelocity.y = 0.0F;
    float DampingFactor{ std::max(0.0F, 1.0F - (Actor.GetLinearDamping() * DeltaTime)) };
    NextVelocity *= DampingFactor;
    DirectX::SimpleMath::Vector3 NextPosition{ Actor.GetPosition() + (NextVelocity * DeltaTime) };

    const IPhysicsActorRepository& ActorRepository{ WorldMediator.GetActorRepository() };
    float SurfaceHeight{};
    bool HasSurfaceHeight{ TryGetHighestTerrainSurfaceHeight(ActorRepository, NextPosition.x, NextPosition.z, SurfaceHeight) };
    if (HasSurfaceHeight) {
        float HalfHeight{ GetActorHalfHeight(Actor) };
        NextPosition.y = SurfaceHeight + HalfHeight + 0.02F;
        NextVelocity.y = 0.0F;
    }

    Actor.SetVelocity(NextVelocity);
    Actor.SetPosition(NextPosition);
    Actor.ClearAccumulatedForce();
}
