#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include "PhysicsLib/Actors/CollisionSolver/PhysicsStaticCollisionSolver.h"
#include "PhysicsLib/Actors/PhysicsActorBase.h"

#undef max
#undef min

#include "PhysicsLib/Actors/PhysicsDynamicCollisionInternal.inl"

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
    if (OtherActor.GetActorType() == PhysicsActorBase::PhysicsActorType::Dynamic) {
        return ResolveDynamicCollision(SelfActor, OtherActor, DeltaTime);
    }

    return false;
}

bool PhysicsStaticCollisionSolver::ResolveDynamicCollision(const PhysicsActorBase& SelfActor, PhysicsActorBase& DynamicActor, float DeltaTime) const {
    if (&SelfActor == &DynamicActor) {
        return false;
    }

    if (SelfActor.GetActorType() != PhysicsActorBase::PhysicsActorType::Static || DynamicActor.GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic) {
        return false;
    }

    if (SelfActor.GetIsActive() == false || DynamicActor.GetIsActive() == false || DynamicActor.GetInverseMass() <= 0.0F) {
        return false;
    }

    DirectX::BoundingOrientedBox StaticBounds{ SelfActor.GetWorldBoundingBox() };
    DirectX::BoundingOrientedBox DynamicBounds{ DynamicActor.GetWorldBoundingBox() };
    if (StaticBounds.Intersects(DynamicBounds) == false) {
        return false;
    }

    DynamicObb StaticObb{ CreateDynamicObb(StaticBounds) };
    DynamicObb DynamicObbValue{ CreateDynamicObb(DynamicBounds) };
    DynamicSatResult SatResult{};
    if (ComputeObbSatResult(StaticObb, DynamicObbValue, SatResult) == false) {
        return false;
    }

    PhysicsActorBase& StaticActor{ const_cast<PhysicsActorBase&>(SelfActor) };
    bool HasResolved{ ResolveCollisionFromSatResult(StaticActor, DynamicActor, SatResult, DeltaTime) };
    if (HasResolved == false) {
        return false;
    }

    DynamicActor.RegisterContactNormal(SatResult.mNormal);
    DynamicActor.SetIsSleeping(false);
    return true;
}
