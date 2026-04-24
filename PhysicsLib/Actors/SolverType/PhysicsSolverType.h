#pragma once

/*
PhysicsLib Header Guide
Role:
- Defines CollisionSolver, Integrater, ConstraintSolver interfaces and policy concepts.
Initialization:
- Do not instantiate the interfaces directly; use concrete solver or integrater implementations.
Usage:
- Defines the functions and inheritance required by policy types passed to PhysicsActor.
Notes:
- Custom policy types must be default constructible and publicly derive from the matching interface.
*/

#include <concepts>

class IPhysicsWorldMediator;
class PhysicsActorBase;

class IPhysicsCollisionSolver {
public:
    IPhysicsCollisionSolver();
    virtual ~IPhysicsCollisionSolver();
    IPhysicsCollisionSolver(const IPhysicsCollisionSolver& Other);
    IPhysicsCollisionSolver& operator=(const IPhysicsCollisionSolver& Other);
    IPhysicsCollisionSolver(IPhysicsCollisionSolver&& Other) noexcept;
    IPhysicsCollisionSolver& operator=(IPhysicsCollisionSolver&& Other) noexcept;

public:
    virtual bool ResolveCollision(PhysicsActorBase& SelfActor, PhysicsActorBase& OtherActor, float DeltaTime) const = 0;
    virtual bool ResolveDynamicCollision(const PhysicsActorBase& SelfActor, PhysicsActorBase& DynamicActor, float DeltaTime) const = 0;
};

class IPhysicsIntegrater {
public:
    IPhysicsIntegrater();
    virtual ~IPhysicsIntegrater();
    IPhysicsIntegrater(const IPhysicsIntegrater& Other);
    IPhysicsIntegrater& operator=(const IPhysicsIntegrater& Other);
    IPhysicsIntegrater(IPhysicsIntegrater&& Other) noexcept;
    IPhysicsIntegrater& operator=(IPhysicsIntegrater&& Other) noexcept;

public:
    virtual void Integrate(IPhysicsWorldMediator& WorldMediator, PhysicsActorBase& Actor, float DeltaTime) const = 0;
};

class IPhysicsConstraintSolver {
public:
    IPhysicsConstraintSolver();
    virtual ~IPhysicsConstraintSolver();
    IPhysicsConstraintSolver(const IPhysicsConstraintSolver& Other);
    IPhysicsConstraintSolver& operator=(const IPhysicsConstraintSolver& Other);
    IPhysicsConstraintSolver(IPhysicsConstraintSolver&& Other) noexcept;
    IPhysicsConstraintSolver& operator=(IPhysicsConstraintSolver&& Other) noexcept;

public:
    virtual void Solve(IPhysicsWorldMediator& WorldMediator, PhysicsActorBase& Actor, float DeltaTime) const = 0;
};

template <typename SolverType>
concept PhysicsCollisionSolverPolicy = std::derived_from<SolverType, IPhysicsCollisionSolver> && std::default_initializable<SolverType>;

template <typename IntegraterType>
concept PhysicsIntegraterPolicy = std::derived_from<IntegraterType, IPhysicsIntegrater> && std::default_initializable<IntegraterType>;

template <typename ConstraintSolverType>
concept PhysicsConstraintSolverPolicy = std::derived_from<ConstraintSolverType, IPhysicsConstraintSolver> && std::default_initializable<ConstraintSolverType>;
