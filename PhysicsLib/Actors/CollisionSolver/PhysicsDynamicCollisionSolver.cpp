#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <utility>

#include "PhysicsLib/Actors/CollisionSolver/PhysicsDynamicCollisionSolver.h"
#include "PhysicsLib/Actors/PhysicsActorBase.h"

#undef max
#undef min

#include "PhysicsLib/Actors/PhysicsDynamicCollisionInternal.inl"

PhysicsDynamicCollisionSolver::PhysicsDynamicCollisionSolver() {
}

PhysicsDynamicCollisionSolver::~PhysicsDynamicCollisionSolver() {
}

PhysicsDynamicCollisionSolver::PhysicsDynamicCollisionSolver(const PhysicsDynamicCollisionSolver& Other)
    : IPhysicsCollisionSolver{ Other } {
}

PhysicsDynamicCollisionSolver& PhysicsDynamicCollisionSolver::operator=(const PhysicsDynamicCollisionSolver& Other) {
    if (this == &Other) {
        return *this;
    }

    IPhysicsCollisionSolver::operator=(Other);
    return *this;
}

PhysicsDynamicCollisionSolver::PhysicsDynamicCollisionSolver(PhysicsDynamicCollisionSolver&& Other) noexcept
    : IPhysicsCollisionSolver{ std::move(Other) } {
}

PhysicsDynamicCollisionSolver& PhysicsDynamicCollisionSolver::operator=(PhysicsDynamicCollisionSolver&& Other) noexcept {
    if (this == &Other) {
        return *this;
    }

    IPhysicsCollisionSolver::operator=(std::move(Other));
    return *this;
}

void PhysicsDynamicCollisionSolver::BeginFrame(std::size_t PairCandidateCount) {
    (void)PairCandidateCount;
}

void PhysicsDynamicCollisionSolver::EndFrame() {
}

bool PhysicsDynamicCollisionSolver::ResolveCollision(PhysicsActorBase& SelfActor, PhysicsActorBase& OtherActor, float DeltaTime) const {
    if (SelfActor.GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic || OtherActor.GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic) {
        return false;
    }

    if (!SelfActor.GetIsActive() || !OtherActor.GetIsActive()) {
        return false;
    }

    if (SelfActor.GetInverseMass() <= 0.0F || OtherActor.GetInverseMass() <= 0.0F) {
        return false;
    }

    DirectX::BoundingOrientedBox SelfBounds{ SelfActor.GetWorldBoundingBox() };
    DirectX::BoundingOrientedBox OtherBounds{ OtherActor.GetWorldBoundingBox() };
    if (!SelfBounds.Intersects(OtherBounds)) {
        return false;
    }

    DynamicObb SelfObb{ CreateDynamicObb(SelfBounds) };
    DynamicObb OtherObb{ CreateDynamicObb(OtherBounds) };
    DynamicSatResult SatResult{};
    if (!ComputeObbSatResult(SelfObb, OtherObb, SatResult)) {
        return false;
    }

    if (!ResolveCollisionFromSatResult(SelfActor, OtherActor, SatResult, DeltaTime)) {
        return false;
    }

    SelfActor.SetIsSleeping(false);
    OtherActor.SetIsSleeping(false);
    return true;
}

bool PhysicsDynamicCollisionSolver::ResolveDynamicCollision(const PhysicsActorBase& SelfActor, PhysicsActorBase& DynamicActor, float DeltaTime) const {
    (void)SelfActor;
    (void)DynamicActor;
    (void)DeltaTime;
    return false;
}
