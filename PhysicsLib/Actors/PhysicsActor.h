#pragma once

/*
PhysicsLib Header Guide
Role:
- Builds concrete Actor types by combining collision, integration, and constraint policies.
Initialization:
- Construct with ActorDesc or a name; normal users should prefer PhysicsWorld creation helpers.
Usage:
- Serves as the base template behind PhysicsDynamicActor, PhysicsKinematicActor, and PhysicsStaticActor.
Notes:
- Template implementation is defined at the bottom of this header, so include paths must stay valid when ported.
*/

#include <memory>
#include <utility>

#include "PhysicsLib/Actors/PhysicsActorBase.h"
#include "PhysicsLib/Actors/SolverType/PhysicsActorSolverType.h"
#include "PhysicsLib/Actors/SolverType/PhysicsSolverType.h"

template <PhysicsCollisionSolverPolicy CollisionSolverType, PhysicsIntegraterPolicy IntegraterType, PhysicsConstraintSolverPolicy ConstraintSolverType = DefaultConstraintSolver, PhysicsActorBase::PhysicsActorType ActorTypeValue = PhysicsActorBase::PhysicsActorType::Dynamic>
class PhysicsActor : public PhysicsActorBase {
public:
    using CollisionSolver = CollisionSolverType;
    using Integrater = IntegraterType;
    using ConstraintSolver = ConstraintSolverType;
    using ActorDesc = PhysicsActorBase::ActorDesc;

public:
    PhysicsActor();
    ~PhysicsActor() override = default;
    PhysicsActor(const PhysicsActor& Other) = default;
    PhysicsActor& operator=(const PhysicsActor& Other) = default;
    PhysicsActor(PhysicsActor&& Other) noexcept = default;
    PhysicsActor& operator=(PhysicsActor&& Other) noexcept = default;

    explicit PhysicsActor(std::string Name);
    explicit PhysicsActor(const ActorDesc& Desc);

public:
    bool ResolveDynamicCollision(PhysicsActorBase& DynamicActor, float DeltaTime) const override;
    bool ResolveActorCollision(PhysicsActorBase& OtherActor, float DeltaTime) override;
    void Integrate(IPhysicsWorldMediator& WorldMediator, float DeltaTime) override;
    void SolveConstraints(IPhysicsWorldMediator& WorldMediator, float DeltaTime) override;
    std::unique_ptr<PhysicsActorBase> Clone() const override;

private:
    void ApplyActorTypeDefaults();

private:
    CollisionSolverType mCollisionSolver;
    IntegraterType mIntegrater;
    ConstraintSolverType mConstraintSolver;
};

template <PhysicsCollisionSolverPolicy CollisionSolverType, PhysicsIntegraterPolicy IntegraterType, PhysicsConstraintSolverPolicy ConstraintSolverType, PhysicsActorBase::PhysicsActorType ActorTypeValue>
PhysicsActor<CollisionSolverType, IntegraterType, ConstraintSolverType, ActorTypeValue>::PhysicsActor()
    : PhysicsActorBase{},
      mCollisionSolver{},
      mIntegrater{},
      mConstraintSolver{} {
    ApplyActorTypeDefaults();
}

template <PhysicsCollisionSolverPolicy CollisionSolverType, PhysicsIntegraterPolicy IntegraterType, PhysicsConstraintSolverPolicy ConstraintSolverType, PhysicsActorBase::PhysicsActorType ActorTypeValue>
PhysicsActor<CollisionSolverType, IntegraterType, ConstraintSolverType, ActorTypeValue>::PhysicsActor(std::string Name)
    : PhysicsActorBase{ std::move(Name) },
      mCollisionSolver{},
      mIntegrater{},
      mConstraintSolver{} {
    ApplyActorTypeDefaults();
}

template <PhysicsCollisionSolverPolicy CollisionSolverType, PhysicsIntegraterPolicy IntegraterType, PhysicsConstraintSolverPolicy ConstraintSolverType, PhysicsActorBase::PhysicsActorType ActorTypeValue>
PhysicsActor<CollisionSolverType, IntegraterType, ConstraintSolverType, ActorTypeValue>::PhysicsActor(const ActorDesc& Desc)
    : PhysicsActorBase{ Desc },
      mCollisionSolver{},
      mIntegrater{},
      mConstraintSolver{} {
    ApplyActorTypeDefaults();
}

template <PhysicsCollisionSolverPolicy CollisionSolverType, PhysicsIntegraterPolicy IntegraterType, PhysicsConstraintSolverPolicy ConstraintSolverType, PhysicsActorBase::PhysicsActorType ActorTypeValue>
bool PhysicsActor<CollisionSolverType, IntegraterType, ConstraintSolverType, ActorTypeValue>::ResolveDynamicCollision(PhysicsActorBase& DynamicActor, float DeltaTime) const {
    bool HasResolved{ mCollisionSolver.ResolveDynamicCollision(*this, DynamicActor, DeltaTime) };
    return HasResolved;
}

template <PhysicsCollisionSolverPolicy CollisionSolverType, PhysicsIntegraterPolicy IntegraterType, PhysicsConstraintSolverPolicy ConstraintSolverType, PhysicsActorBase::PhysicsActorType ActorTypeValue>
bool PhysicsActor<CollisionSolverType, IntegraterType, ConstraintSolverType, ActorTypeValue>::ResolveActorCollision(PhysicsActorBase& OtherActor, float DeltaTime) {
    bool HasResolved{ mCollisionSolver.ResolveCollision(*this, OtherActor, DeltaTime) };
    return HasResolved;
}

template <PhysicsCollisionSolverPolicy CollisionSolverType, PhysicsIntegraterPolicy IntegraterType, PhysicsConstraintSolverPolicy ConstraintSolverType, PhysicsActorBase::PhysicsActorType ActorTypeValue>
void PhysicsActor<CollisionSolverType, IntegraterType, ConstraintSolverType, ActorTypeValue>::Integrate(IPhysicsWorldMediator& WorldMediator, float DeltaTime) {
    mIntegrater.Integrate(WorldMediator, *this, DeltaTime);
}

template <PhysicsCollisionSolverPolicy CollisionSolverType, PhysicsIntegraterPolicy IntegraterType, PhysicsConstraintSolverPolicy ConstraintSolverType, PhysicsActorBase::PhysicsActorType ActorTypeValue>
void PhysicsActor<CollisionSolverType, IntegraterType, ConstraintSolverType, ActorTypeValue>::SolveConstraints(IPhysicsWorldMediator& WorldMediator, float DeltaTime) {
    mConstraintSolver.Solve(WorldMediator, *this, DeltaTime);
}

template <PhysicsCollisionSolverPolicy CollisionSolverType, PhysicsIntegraterPolicy IntegraterType, PhysicsConstraintSolverPolicy ConstraintSolverType, PhysicsActorBase::PhysicsActorType ActorTypeValue>
std::unique_ptr<PhysicsActorBase> PhysicsActor<CollisionSolverType, IntegraterType, ConstraintSolverType, ActorTypeValue>::Clone() const {
    std::unique_ptr<PhysicsActorBase> ClonedActor{ std::make_unique<PhysicsActor<CollisionSolverType, IntegraterType, ConstraintSolverType, ActorTypeValue>>(*this) };
    return ClonedActor;
}

template <PhysicsCollisionSolverPolicy CollisionSolverType, PhysicsIntegraterPolicy IntegraterType, PhysicsConstraintSolverPolicy ConstraintSolverType, PhysicsActorBase::PhysicsActorType ActorTypeValue>
void PhysicsActor<CollisionSolverType, IntegraterType, ConstraintSolverType, ActorTypeValue>::ApplyActorTypeDefaults() {
    SetActorType(ActorTypeValue);

    if constexpr (ActorTypeValue == PhysicsActorBase::PhysicsActorType::Static) {
        SetFlags(GetFlags() | PhysicsActorBase::PhysicsActorFlags::Static);
        SetMass(0.0F);
    }

    if constexpr (ActorTypeValue == PhysicsActorBase::PhysicsActorType::Kinematic) {
        SetFlags(GetFlags() | PhysicsActorBase::PhysicsActorFlags::Kinematic);
    }
}
