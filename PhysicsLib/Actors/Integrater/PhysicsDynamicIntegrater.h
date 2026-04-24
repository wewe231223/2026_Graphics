#pragma once

/*
PhysicsLib Header Guide
Role:
- Integrates Dynamic Actor position and velocity from force, gravity, and damping.
Initialization:
- Direct construction is valid, but normal users get this through the PhysicsDynamicActor policy type.
Usage:
- Used as the PhysicsActor integrater policy and called during world steps.
Notes:
- Inactive, sleeping, or zero inverse mass Actors are skipped.
*/

#include "PhysicsLib/Actors/SolverType/PhysicsSolverType.h"

class PhysicsDynamicIntegrater final : public IPhysicsIntegrater {
public:
    PhysicsDynamicIntegrater();
    ~PhysicsDynamicIntegrater() override;
    PhysicsDynamicIntegrater(const PhysicsDynamicIntegrater& Other);
    PhysicsDynamicIntegrater& operator=(const PhysicsDynamicIntegrater& Other);
    PhysicsDynamicIntegrater(PhysicsDynamicIntegrater&& Other) noexcept;
    PhysicsDynamicIntegrater& operator=(PhysicsDynamicIntegrater&& Other) noexcept;

public:
    void Integrate(IPhysicsWorldMediator& WorldMediator, PhysicsActorBase& Actor, float DeltaTime) const override;
};
