#pragma once

/*
PhysicsLib Header Guide
Role:
- Integrates Kinematic Actor horizontal movement and Terrain surface correction.
Initialization:
- Direct construction is valid, but normal users get this through the PhysicsKinematicActor policy type.
Usage:
- Used as the PhysicsActor integrater policy and called during world steps.
Notes:
- If Terrain exists, the Actor y position is corrected to the highest surface height.
*/

#include "PhysicsLib/Actors/SolverType/PhysicsSolverType.h"

class PhysicsKinematicIntegrater final : public IPhysicsIntegrater {
public:
    PhysicsKinematicIntegrater();
    ~PhysicsKinematicIntegrater() override;
    PhysicsKinematicIntegrater(const PhysicsKinematicIntegrater& Other);
    PhysicsKinematicIntegrater& operator=(const PhysicsKinematicIntegrater& Other);
    PhysicsKinematicIntegrater(PhysicsKinematicIntegrater&& Other) noexcept;
    PhysicsKinematicIntegrater& operator=(PhysicsKinematicIntegrater&& Other) noexcept;

public:
    void Integrate(IPhysicsWorldMediator& WorldMediator, PhysicsActorBase& Actor, float DeltaTime) const override;
};
