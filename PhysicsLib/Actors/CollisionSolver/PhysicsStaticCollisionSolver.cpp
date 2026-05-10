#include <utility>

#include "PhysicsLib/Actors/CollisionSolver/PhysicsStaticCollisionSolver.h"
#include "PhysicsLib/Actors/PhysicsActorBase.h"

PhysicsStaticCollisionSolver::PhysicsStaticCollisionSolver() {
}

PhysicsStaticCollisionSolver::~PhysicsStaticCollisionSolver() {
}

PhysicsStaticCollisionSolver::PhysicsStaticCollisionSolver(const PhysicsStaticCollisionSolver& Other)
    : IPhysicsCollisionSolver{ Other } {
}

PhysicsStaticCollisionSolver& PhysicsStaticCollisionSolver::operator=(const PhysicsStaticCollisionSolver& Other) {
    if (this == &Other) {
        return *this;
    }

    IPhysicsCollisionSolver::operator=(Other);
    return *this;
}

PhysicsStaticCollisionSolver::PhysicsStaticCollisionSolver(PhysicsStaticCollisionSolver&& Other) noexcept
    : IPhysicsCollisionSolver{ std::move(Other) } {
}

PhysicsStaticCollisionSolver& PhysicsStaticCollisionSolver::operator=(PhysicsStaticCollisionSolver&& Other) noexcept {
    if (this == &Other) {
        return *this;
    }

    IPhysicsCollisionSolver::operator=(std::move(Other));
    return *this;
}

bool PhysicsStaticCollisionSolver::ResolveCollision(PhysicsActorBase& SelfActor, PhysicsActorBase& OtherActor, float DeltaTime) const {
    (void)SelfActor;
    (void)OtherActor;
    (void)DeltaTime;
    return false;
}

bool PhysicsStaticCollisionSolver::ResolveDynamicCollision(const PhysicsActorBase& SelfActor, PhysicsActorBase& DynamicActor, float DeltaTime) const {
    (void)SelfActor;
    (void)DynamicActor;
    (void)DeltaTime;
    return false;
}
