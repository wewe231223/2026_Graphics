#pragma once

#include <DirectXTK12/SimpleMath.h>

class IPhysicsActorRepository;
class ITerrainQuery;

class PhysicsKinematicSceneSimulator final {
public:
    PhysicsKinematicSceneSimulator();
    ~PhysicsKinematicSceneSimulator();
    PhysicsKinematicSceneSimulator(const PhysicsKinematicSceneSimulator& Other);
    PhysicsKinematicSceneSimulator& operator=(const PhysicsKinematicSceneSimulator& Other);
    PhysicsKinematicSceneSimulator(PhysicsKinematicSceneSimulator&& Other) noexcept;
    PhysicsKinematicSceneSimulator& operator=(PhysicsKinematicSceneSimulator&& Other) noexcept;

public:
    void Tick(IPhysicsActorRepository& ActorRepository, const ITerrainQuery& TerrainQuery, const DirectX::SimpleMath::Vector3& Gravity, float DeltaTime) const;
};
