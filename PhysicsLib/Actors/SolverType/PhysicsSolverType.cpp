#include "PhysicsLib/Actors/SolverType/PhysicsSolverType.h"

IPhysicsCollisionSolver::IPhysicsCollisionSolver() {
}

IPhysicsCollisionSolver::~IPhysicsCollisionSolver() {
}

IPhysicsCollisionSolver::IPhysicsCollisionSolver(const IPhysicsCollisionSolver& Other) {
    (void)Other;
}

IPhysicsCollisionSolver& IPhysicsCollisionSolver::operator=(const IPhysicsCollisionSolver& Other) {
    (void)Other;
    return *this;
}

IPhysicsCollisionSolver::IPhysicsCollisionSolver(IPhysicsCollisionSolver&& Other) noexcept {
    (void)Other;
}

IPhysicsCollisionSolver& IPhysicsCollisionSolver::operator=(IPhysicsCollisionSolver&& Other) noexcept {
    (void)Other;
    return *this;
}

IPhysicsIntegrater::IPhysicsIntegrater() {
}

IPhysicsIntegrater::~IPhysicsIntegrater() {
}

IPhysicsIntegrater::IPhysicsIntegrater(const IPhysicsIntegrater& Other) {
    (void)Other;
}

IPhysicsIntegrater& IPhysicsIntegrater::operator=(const IPhysicsIntegrater& Other) {
    (void)Other;
    return *this;
}

IPhysicsIntegrater::IPhysicsIntegrater(IPhysicsIntegrater&& Other) noexcept {
    (void)Other;
}

IPhysicsIntegrater& IPhysicsIntegrater::operator=(IPhysicsIntegrater&& Other) noexcept {
    (void)Other;
    return *this;
}

IPhysicsConstraintSolver::IPhysicsConstraintSolver() {
}

IPhysicsConstraintSolver::~IPhysicsConstraintSolver() {
}

IPhysicsConstraintSolver::IPhysicsConstraintSolver(const IPhysicsConstraintSolver& Other) {
    (void)Other;
}

IPhysicsConstraintSolver& IPhysicsConstraintSolver::operator=(const IPhysicsConstraintSolver& Other) {
    (void)Other;
    return *this;
}

IPhysicsConstraintSolver::IPhysicsConstraintSolver(IPhysicsConstraintSolver&& Other) noexcept {
    (void)Other;
}

IPhysicsConstraintSolver& IPhysicsConstraintSolver::operator=(IPhysicsConstraintSolver&& Other) noexcept {
    (void)Other;
    return *this;
}
