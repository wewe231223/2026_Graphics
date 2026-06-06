#pragma once

/*
PhysicsLib Header Guide
Role:
- Provides the collision solver policy for plain Static Actors.
Initialization:
- Direct construction is valid, but normal users get this through the PhysicsStaticActor policy type.
Usage:
- Use it for immovable OBB collision targets, and override the functions in types that need custom Static collision.
Notes:
- Height-field Terrain Actors still use their specialized path.
*/

#include "PhysicsLib/Actors/SolverType/PhysicsSolverType.h"

class PhysicsStaticCollisionSolver final : public IPhysicsCollisionSolver {
public:
    PhysicsStaticCollisionSolver();
    ~PhysicsStaticCollisionSolver() override;
    PhysicsStaticCollisionSolver(const PhysicsStaticCollisionSolver& Other);
    PhysicsStaticCollisionSolver& operator=(const PhysicsStaticCollisionSolver& Other);
    PhysicsStaticCollisionSolver(PhysicsStaticCollisionSolver&& Other) noexcept;
    PhysicsStaticCollisionSolver& operator=(PhysicsStaticCollisionSolver&& Other) noexcept;

public:
    bool ResolveCollision(PhysicsActorBase& SelfActor, PhysicsActorBase& OtherActor, float DeltaTime) const override;
    bool ResolveDynamicCollision(const PhysicsActorBase& SelfActor, PhysicsActorBase& DynamicActor, float DeltaTime) const override;
};
