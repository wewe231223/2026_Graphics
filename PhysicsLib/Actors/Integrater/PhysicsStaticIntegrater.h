#pragma once

/*
PhysicsLib Header Guide
Role:
- Provides the integrater policy for Static Actors.
Initialization:
- Direct construction is valid, but normal users get this through the PhysicsStaticActor policy type.
Usage:
- Used as the PhysicsActor integrater policy; Static Actor transforms are not integrated automatically.
Notes:
- The default implementation intentionally does nothing.
*/

#include "PhysicsLib/Actors/SolverType/PhysicsSolverType.h"

class PhysicsStaticIntegrater final : public IPhysicsIntegrater {
public:
    PhysicsStaticIntegrater();
    ~PhysicsStaticIntegrater() override;
    PhysicsStaticIntegrater(const PhysicsStaticIntegrater& Other);
    PhysicsStaticIntegrater& operator=(const PhysicsStaticIntegrater& Other);
    PhysicsStaticIntegrater(PhysicsStaticIntegrater&& Other) noexcept;
    PhysicsStaticIntegrater& operator=(PhysicsStaticIntegrater&& Other) noexcept;

public:
    void Integrate(IPhysicsWorldMediator& WorldMediator, PhysicsActorBase& Actor, float DeltaTime) const override;
};
