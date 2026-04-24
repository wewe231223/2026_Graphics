#include <algorithm>
#include <utility>

#include "PhysicsLib/Actors/Integrater/PhysicsDynamicIntegrater.h"
#include "PhysicsLib/Actors/PhysicsActorBase.h"
#include "PhysicsLib/Simulation/Mediator/IPhysicsWorldMediator.h"

#undef max
#undef min

PhysicsDynamicIntegrater::PhysicsDynamicIntegrater() {
}

PhysicsDynamicIntegrater::~PhysicsDynamicIntegrater() {
}

PhysicsDynamicIntegrater::PhysicsDynamicIntegrater(const PhysicsDynamicIntegrater& Other)
    : IPhysicsIntegrater{ Other } {
}

PhysicsDynamicIntegrater& PhysicsDynamicIntegrater::operator=(const PhysicsDynamicIntegrater& Other) {
    if (this == &Other) {
        return *this;
    }

    IPhysicsIntegrater::operator=(Other);
    return *this;
}

PhysicsDynamicIntegrater::PhysicsDynamicIntegrater(PhysicsDynamicIntegrater&& Other) noexcept
    : IPhysicsIntegrater{ std::move(Other) } {
}

PhysicsDynamicIntegrater& PhysicsDynamicIntegrater::operator=(PhysicsDynamicIntegrater&& Other) noexcept {
    if (this == &Other) {
        return *this;
    }

    IPhysicsIntegrater::operator=(std::move(Other));
    return *this;
}

void PhysicsDynamicIntegrater::Integrate(IPhysicsWorldMediator& WorldMediator, PhysicsActorBase& Actor, float DeltaTime) const {
    if (Actor.GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic) {
        return;
    }

    if (!Actor.GetIsActive() || Actor.GetInverseMass() <= 0.0F || Actor.GetIsSleeping()) {
        return;
    }

    float ActorMass{ Actor.GetMass() };
    float ActorInverseMass{ Actor.GetInverseMass() };
    DirectX::SimpleMath::Vector3 TotalAcceleration{ WorldMediator.GetGravity() + Actor.GetAcceleration() };
    DirectX::SimpleMath::Vector3 TotalForce{ (TotalAcceleration * ActorMass) + Actor.GetAccumulatedForce() };
    DirectX::SimpleMath::Vector3 NextLinearMomentum{ Actor.GetLinearMomentum() + (TotalForce * DeltaTime) };
    DirectX::SimpleMath::Vector3 NextVelocity{ NextLinearMomentum * ActorInverseMass };
    float DampingFactor{ std::max(0.0F, 1.0F - (Actor.GetLinearDamping() * DeltaTime)) };
    NextVelocity *= DampingFactor;
    NextLinearMomentum = NextVelocity * ActorMass;

    DirectX::SimpleMath::Vector3 NextPosition{ Actor.GetPosition() + (NextVelocity * DeltaTime) };
    Actor.SetPosition(NextPosition);
    Actor.SetVelocity(NextVelocity);
    Actor.SetLinearMomentum(NextLinearMomentum);
    Actor.ClearAccumulatedForce();
    Actor.UpdateSleepState();
}
