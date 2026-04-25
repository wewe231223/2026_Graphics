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
    std::vector<const PhysicsTerrainActor*> TerrainActors{ ActorRepository.CollectTerrainActors() };
    std::size_t TerrainActorCount{ TerrainActors.size() };
    bool HasSurfaceHeight{};
    float HighestSurfaceHeight{};

    for (std::size_t TerrainActorIndex{ 0U }; TerrainActorIndex < TerrainActorCount; ++TerrainActorIndex) {
        const PhysicsTerrainActor* TerrainActor{ TerrainActors[TerrainActorIndex] };
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

float GetActorBottomOffsetFromPositionY(const PhysicsActorBase& Actor) {
    DirectX::XMFLOAT3 Corners[8]{};
    Actor.GetWorldBoundingBox().GetCorners(Corners);

    float MinimumY{ Corners[0].y };
    for (std::size_t CornerIndex{ 1U }; CornerIndex < 8U; ++CornerIndex) {
        if (Corners[CornerIndex].y < MinimumY) {
            MinimumY = Corners[CornerIndex].y;
        }
    }

    return MinimumY - Actor.GetPosition().y;
}

bool ResolveKinematicActorTerrainContact(const IPhysicsActorRepository& ActorRepository, PhysicsActorBase& Actor) {
    if (Actor.HasFlag(PhysicsActorBase::PhysicsActorFlags::IgnoreTerrainCollide)) {
        return false;
    }

    float SurfaceHeight{};
    bool HasSurfaceHeight{ TryGetHighestTerrainSurfaceHeight(ActorRepository, Actor.GetPosition().x, Actor.GetPosition().z, SurfaceHeight) };
    if (!HasSurfaceHeight) {
        return false;
    }

    DirectX::SimpleMath::Vector3 NextPosition{ Actor.GetPosition() };
    const float ActorBottomOffsetFromPositionY{ GetActorBottomOffsetFromPositionY(Actor) };
    const float ActorBottomY{ NextPosition.y + ActorBottomOffsetFromPositionY };
    if (ActorBottomY >= SurfaceHeight || Actor.GetVelocity().y >= 0.0F) {
        return false;
    }

    NextPosition.y = SurfaceHeight - ActorBottomOffsetFromPositionY;

    DirectX::SimpleMath::Vector3 NextVelocity{ Actor.GetVelocity() };
    NextVelocity.y = 0.0F;

    Actor.SetPosition(NextPosition);
    Actor.SetVelocity(NextVelocity);
    return true;
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
    if (Actor.GetActorType() != PhysicsActorBase::PhysicsActorType::Kinematic) {
        return;
    }

    if (!Actor.GetIsActive() || DeltaTime <= 0.0F) {
        return;
    }

    DirectX::SimpleMath::Vector3 NextVelocity{ Actor.GetVelocity() };
    DirectX::SimpleMath::Vector3 Gravity{};
    if (!Actor.HasFlag(PhysicsActorBase::PhysicsActorFlags::IgnoreGravity)) {
        Gravity = WorldMediator.GetGravity();
    }

    DirectX::SimpleMath::Vector3 TotalAcceleration{ Gravity + Actor.GetAcceleration() };
    NextVelocity += TotalAcceleration * DeltaTime;
    float DampingFactor{ std::max(0.0F, 1.0F - (Actor.GetLinearDamping() * DeltaTime)) };
    NextVelocity *= DampingFactor;
    DirectX::SimpleMath::Vector3 NextPosition{ Actor.GetPosition() + (NextVelocity * DeltaTime) };
    Actor.SetVelocity(NextVelocity);
    Actor.SetPosition(NextPosition);

    const IPhysicsActorRepository& ActorRepository{ WorldMediator.GetActorRepository() };
    ResolveKinematicActorTerrainContact(ActorRepository, Actor);
    Actor.ClearAccumulatedForce();
}
