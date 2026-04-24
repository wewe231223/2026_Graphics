#pragma once

/*
PhysicsLib Header Guide
Role:
- Resolves OBB collision response between Dynamic Actors.
Initialization:
- Direct construction is valid, but normal users get this through the PhysicsDynamicActor policy type.
Usage:
- Used as the PhysicsActor collision solver policy, with ResolveCollision handling Dynamic to Dynamic pairs.
Notes:
- BeginFrame and EndFrame are per-step extension points and currently do not keep state.
*/

#include <cstddef>

#include "PhysicsLib/Actors/SolverType/PhysicsSolverType.h"

class PhysicsDynamicCollisionSolver final : public IPhysicsCollisionSolver {
public:
    PhysicsDynamicCollisionSolver();
    ~PhysicsDynamicCollisionSolver() override;
    PhysicsDynamicCollisionSolver(const PhysicsDynamicCollisionSolver& Other);
    PhysicsDynamicCollisionSolver& operator=(const PhysicsDynamicCollisionSolver& Other);
    PhysicsDynamicCollisionSolver(PhysicsDynamicCollisionSolver&& Other) noexcept;
    PhysicsDynamicCollisionSolver& operator=(PhysicsDynamicCollisionSolver&& Other) noexcept;

public:
    bool ResolveCollision(PhysicsActorBase& SelfActor, PhysicsActorBase& OtherActor, float DeltaTime) const override;
    bool ResolveDynamicCollision(const PhysicsActorBase& SelfActor, PhysicsActorBase& DynamicActor, float DeltaTime) const override;
    static void BeginFrame(std::size_t PairCandidateCount);
    static void EndFrame();
};
