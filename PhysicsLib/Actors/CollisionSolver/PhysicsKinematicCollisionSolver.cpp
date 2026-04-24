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

float GetEffectiveRestitution(const PhysicsActorBase& FirstActor, const PhysicsActorBase& SecondActor) {
    float EffectiveRestitution{ std::min(FirstActor.GetRestitution(), SecondActor.GetRestitution()) };
    return EffectiveRestitution;
}

bool IsHeightFieldTerrainActor(const PhysicsActorBase& Actor) {
    const PhysicsTerrainActor* TerrainActor{ dynamic_cast<const PhysicsTerrainActor*>(&Actor) };
    if (TerrainActor == nullptr) {
        return false;
    }

    PhysicsTerrainActor::ActorDesc TerrainActorDesc{ TerrainActor->GetActorDesc() };
    bool HasHeightField{ TerrainActorDesc.HeightFieldWidth > 1U && TerrainActorDesc.HeightFieldHeight > 1U && !TerrainActorDesc.HeightFieldValues.empty() };
    return HasHeightField;
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

bool ResolveKinematicActorPair(PhysicsActorBase& FirstActor, PhysicsActorBase& SecondActor, const DynamicSatResult& SatResult) {
    if (!SatResult.mIntersect) {
        return false;
    }

    DirectX::SimpleMath::Vector3 CollisionNormal{ NormalizeOrZero(SatResult.mNormal) };
    if (IsNearlyZeroVector(CollisionNormal, DynamicSatAxisEpsilon)) {
        return false;
    }

    float PenetrationDepth{ std::max(0.0F, SatResult.mPenetration - KinematicPositionCorrectionSlop) };
    if (PenetrationDepth <= 0.0F) {
        return true;
    }

    float FirstPositionWeight{ 1.0F };
    float SecondPositionWeight{};
    PhysicsActorBase::PhysicsActorType OtherType{ SecondActor.GetActorType() };
    if (OtherType == PhysicsActorBase::PhysicsActorType::Dynamic) {
        FirstPositionWeight = 0.0F;
        SecondPositionWeight = 1.0F;
    } else if (OtherType == PhysicsActorBase::PhysicsActorType::Kinematic) {
        FirstPositionWeight = 0.5F;
        SecondPositionWeight = 0.5F;
    }

    DirectX::SimpleMath::Vector3 PositionCorrection{ CollisionNormal * PenetrationDepth };
    if (FirstPositionWeight > 0.0F) {
        FirstActor.SetPosition(FirstActor.GetPosition() - (PositionCorrection * FirstPositionWeight));
    }

    if (SecondPositionWeight > 0.0F) {
        SecondActor.SetPosition(SecondActor.GetPosition() + (PositionCorrection * SecondPositionWeight));
    }

    float EffectiveRestitution{ GetEffectiveRestitution(FirstActor, SecondActor) };
    if (FirstPositionWeight > 0.0F) {
        ResolveFirstActorVelocity(FirstActor, CollisionNormal, EffectiveRestitution);
    }

    if (SecondPositionWeight > 0.0F && OtherType != PhysicsActorBase::PhysicsActorType::Static) {
        ResolveSecondActorVelocity(SecondActor, CollisionNormal, EffectiveRestitution);
    }

    FirstActor.SetIsSleeping(false);
    if (OtherType != PhysicsActorBase::PhysicsActorType::Static) {
        SecondActor.SetIsSleeping(false);
    }

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
