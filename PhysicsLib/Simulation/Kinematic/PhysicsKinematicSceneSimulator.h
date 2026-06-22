#pragma once

#include <DirectXTK12/SimpleMath.h>

#include "PhysicsLib/Actors/PhysicsKinematicActor.h"

class IPhysicsActorRepository;
class PhysicsActorBase;
namespace Terrain {
    class ITerrainQuery;
}

class PhysicsKinematicSceneSimulator final {
public:
    PhysicsKinematicSceneSimulator();
    ~PhysicsKinematicSceneSimulator();
    PhysicsKinematicSceneSimulator(const PhysicsKinematicSceneSimulator& Other);
    PhysicsKinematicSceneSimulator& operator=(const PhysicsKinematicSceneSimulator& Other);
    PhysicsKinematicSceneSimulator(PhysicsKinematicSceneSimulator&& Other) noexcept;
    PhysicsKinematicSceneSimulator& operator=(PhysicsKinematicSceneSimulator&& Other) noexcept;

public:
    void Tick(IPhysicsActorRepository& ActorRepository, const Terrain::ITerrainQuery& TerrainQuery, const DirectX::SimpleMath::Vector3& Gravity, float DeltaTime) const;

private:
    float GetActorBottomOffsetFromPositionY(const PhysicsActorBase& Actor) const;
    bool ResolveKinematicActorTerrainContact(const Terrain::ITerrainQuery& TerrainQuery, PhysicsActorBase& Actor) const;
    void ResolveKinematicActorStaticContacts(IPhysicsActorRepository& ActorRepository, PhysicsKinematicActor& KinematicActor, float DeltaTime) const;
};
