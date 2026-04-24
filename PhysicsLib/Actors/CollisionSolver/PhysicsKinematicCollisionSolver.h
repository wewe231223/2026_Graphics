#pragma once

/*
PhysicsLib Header Guide
Role:
- Corrects position and velocity when a Kinematic Actor collides with Dynamic, Kinematic, or Static Actors.
Initialization:
- Direct construction is valid, but normal users get this through the PhysicsKinematicActor policy type.
Usage:
- Used as the PhysicsActor collision solver policy and called through ResolveCollision or ResolveDynamicCollision.
Notes:
- HeightField Terrain is excluded because terrain-specific collision handles it.
*/

#include "PhysicsLib/Actors/SolverType/PhysicsSolverType.h"

class PhysicsKinematicCollisionSolver final : public IPhysicsCollisionSolver {
public:
    PhysicsKinematicCollisionSolver();
    ~PhysicsKinematicCollisionSolver() override;
    PhysicsKinematicCollisionSolver(const PhysicsKinematicCollisionSolver& Other);
    PhysicsKinematicCollisionSolver& operator=(const PhysicsKinematicCollisionSolver& Other);
    PhysicsKinematicCollisionSolver(PhysicsKinematicCollisionSolver&& Other) noexcept;
    PhysicsKinematicCollisionSolver& operator=(PhysicsKinematicCollisionSolver&& Other) noexcept;

public:
    bool ResolveCollision(PhysicsActorBase& SelfActor, PhysicsActorBase& OtherActor, float DeltaTime) const override;
    bool ResolveDynamicCollision(const PhysicsActorBase& SelfActor, PhysicsActorBase& DynamicActor, float DeltaTime) const override;
};
