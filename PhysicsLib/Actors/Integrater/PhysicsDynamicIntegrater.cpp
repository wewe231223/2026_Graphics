#include <algorithm>
#include <cmath>
#include <utility>

#include "PhysicsLib/Actors/Integrater/PhysicsDynamicIntegrater.h"
#include "PhysicsLib/Actors/PhysicsActorBase.h"
#include "PhysicsLib/Simulation/Mediator/IPhysicsWorldMediator.h"

#undef max
#undef min

namespace {
constexpr float AngularVelocityEpsilon{ 0.00001F };

DirectX::SimpleMath::Quaternion NormalizeQuaternionOrIdentity(const DirectX::SimpleMath::Quaternion& QuaternionValue) {
    DirectX::SimpleMath::Quaternion NormalizedQuaternion{ QuaternionValue };
    if (NormalizedQuaternion.LengthSquared() <= 0.0F) {
        NormalizedQuaternion = DirectX::SimpleMath::Quaternion{ 0.0F, 0.0F, 0.0F, 1.0F };
    } else {
        NormalizedQuaternion.Normalize();
    }

    return NormalizedQuaternion;
}

DirectX::SimpleMath::Quaternion IntegrateOrientation(const DirectX::SimpleMath::Quaternion& CurrentOrientation, const DirectX::SimpleMath::Vector3& AngularVelocity, float DeltaTime) {
    if (DeltaTime <= 0.0F) {
        return NormalizeQuaternionOrIdentity(CurrentOrientation);
    }

    float AngularSpeed{ AngularVelocity.Length() };
    if (AngularSpeed <= AngularVelocityEpsilon) {
        return NormalizeQuaternionOrIdentity(CurrentOrientation);
    }

    DirectX::SimpleMath::Vector3 RotationAxis{ AngularVelocity / AngularSpeed };
    DirectX::SimpleMath::Quaternion DeltaOrientation{ DirectX::SimpleMath::Quaternion::CreateFromAxisAngle(RotationAxis, AngularSpeed * DeltaTime) };
    DirectX::SimpleMath::Quaternion NextOrientation{ CurrentOrientation * DeltaOrientation };
    DirectX::SimpleMath::Quaternion NormalizedNextOrientation{ NormalizeQuaternionOrIdentity(NextOrientation) };
    return NormalizedNextOrientation;
}
}

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

    if (!Actor.GetIsActive() || Actor.GetInverseMass() <= 0.0F) {
        return;
    }

    DirectX::SimpleMath::Vector3 Gravity{};
    if (!Actor.HasFlag(PhysicsActorBase::PhysicsActorFlags::IgnoreGravity)) {
        Gravity = WorldMediator.GetGravity();
    }

    if (Actor.GetIsSleeping()) {
        float GravityLengthSquared{ Gravity.LengthSquared() };
        float SleepThresholdSquared{ Actor.GetSleepThreshold() * Actor.GetSleepThreshold() };
        if (GravityLengthSquared > SleepThresholdSquared && !Actor.HasSupportingContact(Gravity)) {
            Actor.SetIsSleeping(false);
        }
    }

    if (Actor.GetIsSleeping()) {
        return;
    }

    float ActorMass{ Actor.GetMass() };
    float ActorInverseMass{ Actor.GetInverseMass() };
    DirectX::SimpleMath::Vector3 TotalAcceleration{ Gravity + Actor.GetAcceleration() };
    DirectX::SimpleMath::Vector3 TotalForce{ (TotalAcceleration * ActorMass) + Actor.GetAccumulatedForce() };
    DirectX::SimpleMath::Vector3 NextLinearMomentum{ Actor.GetLinearMomentum() + (TotalForce * DeltaTime) };
    DirectX::SimpleMath::Vector3 NextVelocity{ NextLinearMomentum * ActorInverseMass };
    float DampingFactor{ std::max(0.0F, 1.0F - (Actor.GetLinearDamping() * DeltaTime)) };
    NextVelocity *= DampingFactor;
    NextLinearMomentum = NextVelocity * ActorMass;

    DirectX::SimpleMath::Vector3 NextAngularMomentum{ Actor.GetAngularMomentum() + (Actor.GetTorque() * DeltaTime) };
    float AngularDampingFactor{ std::max(0.0F, 1.0F - (Actor.GetAngularDamping() * DeltaTime)) };
    NextAngularMomentum *= AngularDampingFactor;
    DirectX::SimpleMath::Vector3 NextAngularVelocity{ DirectX::SimpleMath::Vector3::TransformNormal(NextAngularMomentum, Actor.GetInverseInertiaTensorWorld()) };
    DirectX::SimpleMath::Quaternion NextOrientation{ IntegrateOrientation(Actor.GetOrientation(), NextAngularVelocity, DeltaTime) };
    DirectX::SimpleMath::Vector3 NextPosition{ Actor.GetPosition() + (NextVelocity * DeltaTime) };

    Actor.SetPosition(NextPosition);
    Actor.SetVelocity(NextVelocity);
    Actor.SetLinearMomentum(NextLinearMomentum);
    Actor.SetAngularMomentum(NextAngularMomentum);
    Actor.SetOrientation(NextOrientation);
    Actor.SetAngularVelocity(DirectX::SimpleMath::Vector3::TransformNormal(NextAngularMomentum, Actor.GetInverseInertiaTensorWorld()));
    Actor.ClearAccumulatedForce();
    Actor.ClearTorque();
    Actor.UpdateSleepState(Gravity);
}
