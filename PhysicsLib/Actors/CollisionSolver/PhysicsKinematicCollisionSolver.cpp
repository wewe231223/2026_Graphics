#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <utility>

#include "PhysicsLib/Actors/CollisionSolver/PhysicsKinematicCollisionSolver.h"
#include "PhysicsLib/Actors/PhysicsActorBase.h"
#include "PhysicsLib/Actors/PhysicsTerrainActor.h"

#undef max
#undef min

#include "PhysicsLib/Actors/PhysicsDynamicCollisionInternal.inl"

namespace {
constexpr float KinematicPositionCorrectionSlop{ 0.002F };
constexpr float KinematicDynamicImpulseMagnitudeClamp{ 1000.0F };

float GetEffectiveRestitution(const PhysicsActorBase& FirstActor, const PhysicsActorBase& SecondActor) {
    float EffectiveRestitution{ std::min(FirstActor.GetRestitution(), SecondActor.GetRestitution()) };
    return EffectiveRestitution;
}

bool IsHeightFieldTerrainActor(const PhysicsActorBase& Actor) {
    if (Actor.IsTerrainActor() == false) {
        return false;
    }

    const PhysicsTerrainActor& TerrainActor{ static_cast<const PhysicsTerrainActor&>(Actor) };
    bool HasHeightField{ TerrainActor.HasHeightFieldData() };
    return HasHeightField;
}

void ResolveSecondActorVelocity(PhysicsActorBase& SecondActor, const DirectX::SimpleMath::Vector3& CollisionNormal, float Restitution) {
    DirectX::SimpleMath::Vector3 SecondVelocity{ SecondActor.GetVelocity() };
    float VelocityAlongNormal{ SecondVelocity.Dot(CollisionNormal) };
    if (VelocityAlongNormal >= 0.0F) {
        return;
    }

    float VelocityCorrection{ VelocityAlongNormal * (1.0F + Restitution) };
    SecondVelocity -= CollisionNormal * VelocityCorrection;
    SecondActor.SetVelocity(SecondVelocity);
}

void ResolveFirstActorVelocity(PhysicsActorBase& FirstActor, const DirectX::SimpleMath::Vector3& CollisionNormal, float Restitution) {
    DirectX::SimpleMath::Vector3 FirstVelocity{ FirstActor.GetVelocity() };
    float VelocityAlongNormal{ FirstVelocity.Dot(CollisionNormal) };
    if (VelocityAlongNormal <= 0.0F) {
        return;
    }

    float VelocityCorrection{ VelocityAlongNormal * (1.0F + Restitution) };
    FirstVelocity -= CollisionNormal * VelocityCorrection;
    FirstActor.SetVelocity(FirstVelocity);
}

bool ApplyKinematicDynamicContactImpulse(const PhysicsActorBase& FirstActor, PhysicsActorBase& SecondActor, const DynamicSatResult& SatResult, const DirectX::SimpleMath::Vector3& CollisionNormal) {
    if (SecondActor.GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic || SecondActor.GetInverseMass() <= 0.0F) {
        return false;
    }

    DirectX::BoundingOrientedBox FirstBounds{ FirstActor.GetWorldBoundingBox() };
    DirectX::BoundingOrientedBox SecondBounds{ SecondActor.GetWorldBoundingBox() };
    DynamicObb FirstObb{ CreateDynamicObb(FirstBounds) };
    DynamicObb SecondObb{ CreateDynamicObb(SecondBounds) };
    DynamicContactManifold ContactManifold{ BuildContactManifold(FirstBounds, SecondBounds, FirstObb, SecondObb, SatResult) };
    if (ContactManifold.mContactPointCount == 0U) {
        return false;
    }

    float EffectiveRestitution{ GetEffectiveRestitution(FirstActor, SecondActor) };
    float SafeContactPointCount{ static_cast<float>(std::max<std::size_t>(ContactManifold.mContactPointCount, 1U)) };
    float ContactImpulseClamp{ KinematicDynamicImpulseMagnitudeClamp / SafeContactPointCount };
    bool HasAppliedImpulse{};
    for (std::size_t ContactPointIndex{ 0U }; ContactPointIndex < ContactManifold.mContactPointCount; ++ContactPointIndex) {
        const DynamicContactPoint& ContactPoint{ ContactManifold.mContactPoints[ContactPointIndex] };
        DirectX::SimpleMath::Vector3 DynamicContactVelocity{ CalculateVelocityAtPoint(SecondActor, ContactPoint.mPosition) };
        DirectX::SimpleMath::Vector3 RelativeVelocity{ DynamicContactVelocity - FirstActor.GetVelocity() };
        float VelocityAlongNormal{ RelativeVelocity.Dot(CollisionNormal) };
        if (VelocityAlongNormal >= 0.0F) {
            continue;
        }

        DirectX::SimpleMath::Vector3 ContactOffset{ ContactPoint.mPosition - SecondActor.GetPosition() };
        float NormalDenominator{ CalculateSingleActorContactImpulseDenominator(SecondActor, ContactOffset, CollisionNormal) };
        if (NormalDenominator <= DynamicSatAxisEpsilon) {
            continue;
        }

        float ImpulseMagnitude{ (-(1.0F + EffectiveRestitution) * VelocityAlongNormal) / NormalDenominator };
        ImpulseMagnitude /= SafeContactPointCount;
        ImpulseMagnitude = std::clamp(ImpulseMagnitude, 0.0F, ContactImpulseClamp);
        if (ImpulseMagnitude <= 0.0F) {
            continue;
        }

        DirectX::SimpleMath::Vector3 NormalImpulse{ CollisionNormal * ImpulseMagnitude };
        SecondActor.ApplyImpulseAtPoint(NormalImpulse, ContactPoint.mPosition);
        HasAppliedImpulse = true;
    }

    return HasAppliedImpulse;
}

bool ResolveKinematicActorPair(PhysicsActorBase& FirstActor, PhysicsActorBase& SecondActor, const DynamicSatResult& SatResult) {
    if (!SatResult.mIntersect) {
        return false;
    }

    DirectX::SimpleMath::Vector3 CollisionNormal{ NormalizeOrZero(SatResult.mNormal) };
    if (IsNearlyZeroVector(CollisionNormal, DynamicSatAxisEpsilon)) {
        return false;
    }

    if (SecondActor.GetActorType() == PhysicsActorBase::PhysicsActorType::Static) {
        float PenetrationDepth{ std::max(0.0F, SatResult.mPenetration - KinematicPositionCorrectionSlop) };
        FirstActor.RegisterContactNormal(-CollisionNormal);
        if (PenetrationDepth > 0.0F) {
            FirstActor.SetPosition(FirstActor.GetPosition() - (CollisionNormal * PenetrationDepth));
        }

        float EffectiveRestitution{ GetEffectiveRestitution(FirstActor, SecondActor) };
        ResolveFirstActorVelocity(FirstActor, CollisionNormal, EffectiveRestitution);
        return true;
    }

    if (SecondActor.GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic) {
        return true;
    }

    float PenetrationDepth{ std::max(0.0F, SatResult.mPenetration - KinematicPositionCorrectionSlop) };
    SecondActor.RegisterContactNormal(CollisionNormal);

    bool HasAppliedImpulse{ ApplyKinematicDynamicContactImpulse(FirstActor, SecondActor, SatResult, CollisionNormal) };
    if (HasAppliedImpulse) {
        SecondActor.SetIsSleeping(false);
    }

    if (PenetrationDepth <= 0.0F) {
        return true;
    }

    DirectX::SimpleMath::Vector3 PositionCorrection{ CollisionNormal * PenetrationDepth };
    SecondActor.SetPosition(SecondActor.GetPosition() + PositionCorrection);
    float EffectiveRestitution{ GetEffectiveRestitution(FirstActor, SecondActor) };
    ResolveSecondActorVelocity(SecondActor, CollisionNormal, EffectiveRestitution);
    SecondActor.SetIsSleeping(false);
    return true;
}

bool ResolveKinematicAgainstDynamicActor(const PhysicsActorBase& FirstActor, PhysicsActorBase& SecondActor, const DynamicSatResult& SatResult) {
    if (!SatResult.mIntersect) {
        return false;
    }

    DirectX::SimpleMath::Vector3 CollisionNormal{ NormalizeOrZero(SatResult.mNormal) };
    if (IsNearlyZeroVector(CollisionNormal, DynamicSatAxisEpsilon)) {
        return false;
    }

    float PenetrationDepth{ std::max(0.0F, SatResult.mPenetration - KinematicPositionCorrectionSlop) };
    SecondActor.RegisterContactNormal(CollisionNormal);
    bool HasAppliedImpulse{ ApplyKinematicDynamicContactImpulse(FirstActor, SecondActor, SatResult, CollisionNormal) };
    if (HasAppliedImpulse) {
        SecondActor.SetIsSleeping(false);
    }

    if (PenetrationDepth <= 0.0F) {
        return true;
    }

    SecondActor.SetPosition(SecondActor.GetPosition() + (CollisionNormal * PenetrationDepth));
    float EffectiveRestitution{ GetEffectiveRestitution(FirstActor, SecondActor) };
    ResolveSecondActorVelocity(SecondActor, CollisionNormal, EffectiveRestitution);
    SecondActor.SetIsSleeping(false);
    return true;
}
}

PhysicsKinematicCollisionSolver::PhysicsKinematicCollisionSolver() {
}

PhysicsKinematicCollisionSolver::~PhysicsKinematicCollisionSolver() {
}

PhysicsKinematicCollisionSolver::PhysicsKinematicCollisionSolver(const PhysicsKinematicCollisionSolver& Other)
    : IPhysicsCollisionSolver{ Other } {
}

PhysicsKinematicCollisionSolver& PhysicsKinematicCollisionSolver::operator=(const PhysicsKinematicCollisionSolver& Other) {
    if (this == &Other) {
        return *this;
    }

    IPhysicsCollisionSolver::operator=(Other);
    return *this;
}

PhysicsKinematicCollisionSolver::PhysicsKinematicCollisionSolver(PhysicsKinematicCollisionSolver&& Other) noexcept
    : IPhysicsCollisionSolver{ std::move(Other) } {
}

PhysicsKinematicCollisionSolver& PhysicsKinematicCollisionSolver::operator=(PhysicsKinematicCollisionSolver&& Other) noexcept {
    if (this == &Other) {
        return *this;
    }

    IPhysicsCollisionSolver::operator=(std::move(Other));
    return *this;
}

bool PhysicsKinematicCollisionSolver::ResolveCollision(PhysicsActorBase& SelfActor, PhysicsActorBase& OtherActor, float DeltaTime) const {
    (void)DeltaTime;

    if (&SelfActor == &OtherActor) {
        return false;
    }

    if (SelfActor.GetActorType() != PhysicsActorBase::PhysicsActorType::Kinematic) {
        return false;
    }

    PhysicsActorBase::PhysicsActorType OtherActorType{ OtherActor.GetActorType() };
    bool IsSupportedOtherActorType{ OtherActorType == PhysicsActorBase::PhysicsActorType::Dynamic || OtherActorType == PhysicsActorBase::PhysicsActorType::Kinematic || OtherActorType == PhysicsActorBase::PhysicsActorType::Static };
    if (!IsSupportedOtherActorType) {
        return false;
    }

    if (OtherActorType == PhysicsActorBase::PhysicsActorType::Static && IsHeightFieldTerrainActor(OtherActor)) {
        return false;
    }

    if (!SelfActor.GetIsActive() || !OtherActor.GetIsActive()) {
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

    bool HasResolved{ ResolveKinematicActorPair(SelfActor, OtherActor, SatResult) };
    return HasResolved;
}

bool PhysicsKinematicCollisionSolver::ResolveDynamicCollision(const PhysicsActorBase& SelfActor, PhysicsActorBase& DynamicActor, float DeltaTime) const {
    (void)DeltaTime;

    if (&SelfActor == &DynamicActor) {
        return false;
    }

    if (SelfActor.GetActorType() != PhysicsActorBase::PhysicsActorType::Kinematic || DynamicActor.GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic) {
        return false;
    }

    if (!SelfActor.GetIsActive() || !DynamicActor.GetIsActive()) {
        return false;
    }

    DirectX::BoundingOrientedBox SelfBounds{ SelfActor.GetWorldBoundingBox() };
    DirectX::BoundingOrientedBox OtherBounds{ DynamicActor.GetWorldBoundingBox() };
    if (!SelfBounds.Intersects(OtherBounds)) {
        return false;
    }

    DynamicObb SelfObb{ CreateDynamicObb(SelfBounds) };
    DynamicObb OtherObb{ CreateDynamicObb(OtherBounds) };
    DynamicSatResult SatResult{};
    if (!ComputeObbSatResult(SelfObb, OtherObb, SatResult)) {
        return false;
    }

    bool HasResolved{ ResolveKinematicAgainstDynamicActor(SelfActor, DynamicActor, SatResult) };
    return HasResolved;
}
