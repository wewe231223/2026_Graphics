#pragma once

/*
PhysicsLib Header Guide
Role:
- Provides the default constraint solver policy that performs no constraint work.
Initialization:
- Direct construction is valid, but default Actor policies create it automatically.
Usage:
- Used as the default ConstraintSolver policy for PhysicsActor.
Notes:
- Add another solver with the same interface when joints, distance limits, or fixed limits are needed.
*/

#include "PhysicsLib/Actors/SolverType/PhysicsSolverType.h"

class PhysicsNoConstraintSolver final : public IPhysicsConstraintSolver {
public:
    PhysicsNoConstraintSolver();
    ~PhysicsNoConstraintSolver() override;
    PhysicsNoConstraintSolver(const PhysicsNoConstraintSolver& Other);
    PhysicsNoConstraintSolver& operator=(const PhysicsNoConstraintSolver& Other);
    PhysicsNoConstraintSolver(PhysicsNoConstraintSolver&& Other) noexcept;
    PhysicsNoConstraintSolver& operator=(PhysicsNoConstraintSolver&& Other) noexcept;

public:
    void Solve(IPhysicsWorldMediator& WorldMediator, PhysicsActorBase& Actor, float DeltaTime) const override;
};
