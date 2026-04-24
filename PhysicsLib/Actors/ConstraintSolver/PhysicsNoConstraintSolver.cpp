#include <utility>

#include "PhysicsLib/Actors/ConstraintSolver/PhysicsNoConstraintSolver.h"
#include "PhysicsLib/Actors/PhysicsActorBase.h"
#include "PhysicsLib/Simulation/Mediator/IPhysicsWorldMediator.h"

PhysicsNoConstraintSolver::PhysicsNoConstraintSolver() {
}

PhysicsNoConstraintSolver::~PhysicsNoConstraintSolver() {
}

PhysicsNoConstraintSolver::PhysicsNoConstraintSolver(const PhysicsNoConstraintSolver& Other)
    : IPhysicsConstraintSolver{ Other } {
}

PhysicsNoConstraintSolver& PhysicsNoConstraintSolver::operator=(const PhysicsNoConstraintSolver& Other) {
    if (this == &Other) {
        return *this;
    }

    IPhysicsConstraintSolver::operator=(Other);
    return *this;
}

PhysicsNoConstraintSolver::PhysicsNoConstraintSolver(PhysicsNoConstraintSolver&& Other) noexcept
    : IPhysicsConstraintSolver{ std::move(Other) } {
}

PhysicsNoConstraintSolver& PhysicsNoConstraintSolver::operator=(PhysicsNoConstraintSolver&& Other) noexcept {
    if (this == &Other) {
        return *this;
    }

    IPhysicsConstraintSolver::operator=(std::move(Other));
    return *this;
}

void PhysicsNoConstraintSolver::Solve(IPhysicsWorldMediator& WorldMediator, PhysicsActorBase& Actor, float DeltaTime) const {
    (void)WorldMediator;
    (void)Actor;
    (void)DeltaTime;
}
