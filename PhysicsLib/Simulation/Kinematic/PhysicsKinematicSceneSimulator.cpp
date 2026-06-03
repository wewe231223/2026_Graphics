#include "PhysicsLib/Simulation/Kinematic/PhysicsKinematicSceneSimulator.h"

#include <cstddef>
#include <vector>

#include "PhysicsLib/Actors/PhysicsActorBase.h"
#include "PhysicsLib/Actors/PhysicsKinematicActor.h"
#include "PhysicsLib/Simulation/Repository/IPhysicsActorRepository.h"
#include "PhysicsLib/Terrain/TerrainDataRepository.h"

namespace {
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

bool ResolveKinematicActorTerrainContact(const ITerrainQuery& TerrainQuery, PhysicsActorBase& Actor) {
    if (Actor.HasFlag(PhysicsActorBase::PhysicsActorFlags::IgnoreTerrainCollide)) {
        return false;
    }

    float SurfaceHeight{};
    bool HasSurfaceHeight{ TerrainQuery.TryGetSurfaceHeightAtWorldPosition(Actor.GetPosition().x, Actor.GetPosition().z, SurfaceHeight) };
    if (HasSurfaceHeight == false) {
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

PhysicsKinematicSceneSimulator::PhysicsKinematicSceneSimulator() {
}

PhysicsKinematicSceneSimulator::~PhysicsKinematicSceneSimulator() {
}

PhysicsKinematicSceneSimulator::PhysicsKinematicSceneSimulator(const PhysicsKinematicSceneSimulator& Other) {
    (void)Other;
}

PhysicsKinematicSceneSimulator& PhysicsKinematicSceneSimulator::operator=(const PhysicsKinematicSceneSimulator& Other) {
    (void)Other;
    return *this;
}

PhysicsKinematicSceneSimulator::PhysicsKinematicSceneSimulator(PhysicsKinematicSceneSimulator&& Other) noexcept {
    (void)Other;
}

PhysicsKinematicSceneSimulator& PhysicsKinematicSceneSimulator::operator=(PhysicsKinematicSceneSimulator&& Other) noexcept {
    (void)Other;
    return *this;
}

void PhysicsKinematicSceneSimulator::Tick(IPhysicsActorRepository& ActorRepository, const ITerrainQuery& TerrainQuery, const DirectX::SimpleMath::Vector3& Gravity, float DeltaTime) const {
    if (DeltaTime <= 0.0F) {
        return;
    }

    std::vector<PhysicsKinematicActor*> KinematicActors{};
    ActorRepository.CollectKinematicActors(KinematicActors);
    const std::size_t KinematicActorCount{ KinematicActors.size() };
    for (std::size_t ActorIndex{ 0U }; ActorIndex < KinematicActorCount; ++ActorIndex) {
        PhysicsKinematicActor* KinematicActor{ KinematicActors[ActorIndex] };
        if (KinematicActor == nullptr || KinematicActor->GetIsActive() == false) {
            continue;
        }

        DirectX::SimpleMath::Vector3 NextVelocity{ KinematicActor->GetVelocity() };
        if (KinematicActor->HasFlag(PhysicsActorBase::PhysicsActorFlags::IgnoreGravity) == false) {
            NextVelocity += Gravity * DeltaTime;
        }

        NextVelocity += KinematicActor->GetAcceleration() * DeltaTime;
        const DirectX::SimpleMath::Vector3 NextPosition{ KinematicActor->GetPosition() + (NextVelocity * DeltaTime) };
        KinematicActor->SetVelocity(NextVelocity);
        KinematicActor->SetPosition(NextPosition);
        static_cast<void>(ResolveKinematicActorTerrainContact(TerrainQuery, *KinematicActor));
        KinematicActor->ClearAccumulatedForce();
        KinematicActor->ClearTorque();
    }
}
