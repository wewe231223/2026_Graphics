#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

#include "PhysicsLib/Actors/PhysicsTerrainActor.h"

#undef min
#undef max

namespace {
    constexpr float TerrainContactEpsilon{ 1.0E-4F };
    constexpr float TerrainRestitutionThreshold{ 1.25F };
    constexpr float TerrainPenetrationVelocityFactor{ 0.04F };
    constexpr float TerrainMaximumBiasVelocity{ 0.5F };
    constexpr float TerrainPositionCorrectionFactor{ 1.0F };
    constexpr float TerrainPositionCorrectionSlop{ 0.0005F };
    DirectX::SimpleMath::Vector3 NormalizeOrZero(const DirectX::SimpleMath::Vector3& Value) {
        const float LengthSquared{ Value.LengthSquared() };
        if (LengthSquared <= TerrainContactEpsilon * TerrainContactEpsilon) {
            return DirectX::SimpleMath::Vector3{};
        }

        DirectX::SimpleMath::Vector3 NormalizedValue{ Value / std::sqrt(LengthSquared) };
        return NormalizedValue;
    }

    DirectX::SimpleMath::Vector3 CalculateVelocityAtPoint(const PhysicsActorBase& Actor, const DirectX::SimpleMath::Vector3& WorldPoint) {
        DirectX::SimpleMath::Vector3 ContactOffset{ WorldPoint - Actor.GetPosition() };
        DirectX::SimpleMath::Vector3 AngularVelocityContribution{ Actor.GetAngularVelocity().Cross(ContactOffset) };
        DirectX::SimpleMath::Vector3 PointVelocity{ Actor.GetVelocity() + AngularVelocityContribution };
        return PointVelocity;
    }

    float CalculateContactImpulseDenominator(const PhysicsActorBase& Actor, const DirectX::SimpleMath::Vector3& ContactOffset, const DirectX::SimpleMath::Vector3& Direction) {
        DirectX::SimpleMath::Vector3 RadiusCrossDirection{ ContactOffset.Cross(Direction) };
        DirectX::SimpleMath::Vector3 AngularVelocityDelta{ DirectX::SimpleMath::Vector3::TransformNormal(RadiusCrossDirection, Actor.GetInverseInertiaTensorWorld()) };
        DirectX::SimpleMath::Vector3 ContactVelocityDelta{ AngularVelocityDelta.Cross(ContactOffset) };
        float Denominator{ Actor.GetInverseMass() + Direction.Dot(ContactVelocityDelta) };
        return std::max(0.0F, Denominator);
    }

    bool ApplyTerrainContactImpulse(PhysicsActorBase& DynamicActor, const DirectX::SimpleMath::Vector3& ContactPoint, const DirectX::SimpleMath::Vector3& ContactNormal, float PenetrationDepth, float TerrainFriction, float DeltaTime) {
        DirectX::SimpleMath::Vector3 NormalizedContactNormal{ NormalizeOrZero(ContactNormal) };
        if (NormalizedContactNormal.LengthSquared() <= TerrainContactEpsilon * TerrainContactEpsilon) {
            return false;
        }

        DirectX::SimpleMath::Vector3 ContactOffset{ ContactPoint - DynamicActor.GetPosition() };
        DirectX::SimpleMath::Vector3 ContactVelocity{ CalculateVelocityAtPoint(DynamicActor, ContactPoint) };
        float ContactNormalVelocity{ ContactVelocity.Dot(NormalizedContactNormal) };
        float EffectiveRestitution{};
        if (ContactNormalVelocity < -TerrainRestitutionThreshold) {
            EffectiveRestitution = std::clamp(DynamicActor.GetRestitution(), 0.0F, 1.0F);
        }

        float InverseDeltaTime{ DeltaTime > TerrainContactEpsilon ? 1.0F / DeltaTime : 0.0F };
        float BiasVelocity{ std::max(0.0F, PenetrationDepth - TerrainPositionCorrectionSlop) * TerrainPenetrationVelocityFactor * InverseDeltaTime };
        BiasVelocity = std::min(BiasVelocity, TerrainMaximumBiasVelocity);
        float RestitutionVelocity{ ContactNormalVelocity < 0.0F ? -ContactNormalVelocity * EffectiveRestitution : 0.0F };
        float TargetNormalVelocity{ std::max(BiasVelocity, RestitutionVelocity) };
        float NormalVelocityDelta{ TargetNormalVelocity - ContactNormalVelocity };
        if (NormalVelocityDelta <= 0.0F) {
            NormalVelocityDelta = 0.0F;
        }

        float NormalDenominator{ CalculateContactImpulseDenominator(DynamicActor, ContactOffset, NormalizedContactNormal) };
        if (NormalDenominator <= TerrainContactEpsilon) {
            return false;
        }

        float NormalImpulseMagnitude{ NormalVelocityDelta / NormalDenominator };
        if (NormalImpulseMagnitude < 0.0F) {
            NormalImpulseMagnitude = 0.0F;
        }

        if (NormalImpulseMagnitude > 0.0F) {
            DirectX::SimpleMath::Vector3 NormalImpulse{ NormalizedContactNormal * NormalImpulseMagnitude };
            DynamicActor.ApplyImpulseAtPoint(NormalImpulse, ContactPoint);
        }

        DirectX::SimpleMath::Vector3 ContactVelocityAfterNormal{ CalculateVelocityAtPoint(DynamicActor, ContactPoint) };
        float VelocityAfterNormalProjection{ ContactVelocityAfterNormal.Dot(NormalizedContactNormal) };
        DirectX::SimpleMath::Vector3 TangentialVelocity{ ContactVelocityAfterNormal - (NormalizedContactNormal * VelocityAfterNormalProjection) };
        float TangentialVelocityLengthSquared{ TangentialVelocity.LengthSquared() };
        if (TangentialVelocityLengthSquared > TerrainContactEpsilon * TerrainContactEpsilon && NormalImpulseMagnitude > 0.0F) {
            DirectX::SimpleMath::Vector3 Tangent{ TangentialVelocity / std::sqrt(TangentialVelocityLengthSquared) };
            float TangentDenominator{ CalculateContactImpulseDenominator(DynamicActor, ContactOffset, Tangent) };
            if (TangentDenominator > TerrainContactEpsilon) {
                float FrictionImpulseMagnitude{ -ContactVelocityAfterNormal.Dot(Tangent) / TangentDenominator };
                float EffectiveFriction{ std::sqrt(std::max(0.0F, DynamicActor.GetFriction() * TerrainFriction)) };
                float MaximumFrictionImpulse{ std::abs(NormalImpulseMagnitude) * EffectiveFriction };
                FrictionImpulseMagnitude = std::clamp(FrictionImpulseMagnitude, -MaximumFrictionImpulse, MaximumFrictionImpulse);
                if (std::abs(FrictionImpulseMagnitude) > TerrainContactEpsilon) {
                    DirectX::SimpleMath::Vector3 FrictionImpulse{ Tangent * FrictionImpulseMagnitude };
                    DynamicActor.ApplyImpulseAtPoint(FrictionImpulse, ContactPoint);
                }
            }
        }

        float CorrectedPenetration{ std::max(0.0F, PenetrationDepth - TerrainPositionCorrectionSlop) };
        if (CorrectedPenetration > 0.0F) {
            DirectX::SimpleMath::Vector3 CorrectedPosition{ DynamicActor.GetPosition() + (NormalizedContactNormal * CorrectedPenetration * TerrainPositionCorrectionFactor) };
            DynamicActor.SetPosition(CorrectedPosition);
        }

        return NormalImpulseMagnitude > 0.0F || CorrectedPenetration > 0.0F;
    }
}

PhysicsTerrainActor::PhysicsTerrainActor()
    : PhysicsStaticActor{},
      mTerrainWorldData{ std::make_shared<const TerrainWorldData>() } {
}

PhysicsTerrainActor::~PhysicsTerrainActor() {
}

PhysicsTerrainActor::PhysicsTerrainActor(const PhysicsTerrainActor& Other)
    : PhysicsStaticActor{ Other },
      mTerrainWorldData{ Other.mTerrainWorldData } {
}

PhysicsTerrainActor& PhysicsTerrainActor::operator=(const PhysicsTerrainActor& Other) {
    if (this == &Other) {
        return *this;
    }

    PhysicsStaticActor::operator=(Other);
    mTerrainWorldData = Other.mTerrainWorldData;

    return *this;
}

PhysicsTerrainActor::PhysicsTerrainActor(PhysicsTerrainActor&& Other) noexcept
    : PhysicsStaticActor{ std::move(Other) },
      mTerrainWorldData{ std::move(Other.mTerrainWorldData) } {
    Other.mTerrainWorldData = std::make_shared<const TerrainWorldData>();
}

PhysicsTerrainActor& PhysicsTerrainActor::operator=(PhysicsTerrainActor&& Other) noexcept {
    if (this == &Other) {
        return *this;
    }

    PhysicsStaticActor::operator=(std::move(Other));
    mTerrainWorldData = std::move(Other.mTerrainWorldData);
    Other.mTerrainWorldData = std::make_shared<const TerrainWorldData>();

    return *this;
}

PhysicsTerrainActor::PhysicsTerrainActor(const ActorDesc& Desc)
    : PhysicsStaticActor{},
      mTerrainWorldData{} {
    SetActorDesc(Desc);
}

void PhysicsTerrainActor::SetActorDesc(const ActorDesc& Desc) {
    SetPosition(Desc.Position);
    SetRotation(Desc.Rotation);
    SetScale(Desc.Scale);
    const std::uint32_t TerrainId{ Desc.mTerrainWorldData != nullptr ? Desc.mTerrainWorldData->mTerrainId : 0U };
    mTerrainWorldData = std::make_shared<const TerrainWorldData>(BuildTerrainWorldDataFromActorDesc(Desc, TerrainId));
}

PhysicsTerrainActor::ActorDesc PhysicsTerrainActor::GetActorDesc() const {
    ActorDesc Desc{};
    const TerrainWorldData* TerrainData{ GetTerrainDataPointer() };
    if (TerrainData == nullptr) {
        return Desc;
    }

    Desc = BuildActorDescFromTerrainWorldData(*TerrainData);
    return Desc;
}

const std::shared_ptr<const TerrainWorldData>& PhysicsTerrainActor::GetTerrainWorldData() const {
    return mTerrainWorldData;
}

TerrainWorldData PhysicsTerrainActor::BuildTerrainWorldDataFromActorDesc(const ActorDesc& Desc, std::uint32_t TerrainId) {
    TerrainWorldData TerrainData{};
    if (Desc.mTerrainWorldData != nullptr) {
        TerrainData = *Desc.mTerrainWorldData;
    }

    TerrainData.mTerrainId = TerrainId;
    TerrainData.mPosition = Desc.Position;
    TerrainData.mRotation = Desc.Rotation;
    TerrainData.mOrientation = DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(Desc.Rotation.y, Desc.Rotation.x, Desc.Rotation.z);
    if (TerrainData.mOrientation.LengthSquared() <= 0.0F) {
        TerrainData.mOrientation = DirectX::SimpleMath::Quaternion{ 0.0F, 0.0F, 0.0F, 1.0F };
    }
    else {
        TerrainData.mOrientation.Normalize();
    }

    TerrainData.mScale = Desc.Scale;
    if (Desc.HeightFieldValues.empty() == false) {
        TerrainData.mHalfExtentX = Desc.HalfExtentX;
        TerrainData.mHalfExtentZ = Desc.HalfExtentZ;
        TerrainData.mHeightFieldWidth = Desc.HeightFieldWidth;
        TerrainData.mHeightFieldHeight = Desc.HeightFieldHeight;
        TerrainData.mHeightFieldCellSizeX = Desc.HeightFieldCellSizeX;
        TerrainData.mHeightFieldCellSizeZ = Desc.HeightFieldCellSizeZ;
        TerrainData.mHeightFieldMaxHeight = Desc.HeightFieldMaxHeight;
        TerrainData.mHeightFieldCenterOrigin = Desc.HeightFieldCenterOrigin;
        TerrainData.mHeightFieldValues = Desc.HeightFieldValues;
    }

    return TerrainData;
}

PhysicsTerrainActor::ActorDesc PhysicsTerrainActor::BuildActorDescFromTerrainWorldData(const TerrainWorldData& TerrainData) {
    ActorDesc Desc{};
    Desc.Position = TerrainData.mPosition;
    Desc.Rotation = TerrainData.mRotation;
    Desc.Scale = TerrainData.mScale;
    Desc.HalfExtentX = TerrainData.mHalfExtentX;
    Desc.HalfExtentZ = TerrainData.mHalfExtentZ;
    Desc.HeightFieldWidth = TerrainData.mHeightFieldWidth;
    Desc.HeightFieldHeight = TerrainData.mHeightFieldHeight;
    Desc.HeightFieldCellSizeX = TerrainData.mHeightFieldCellSizeX;
    Desc.HeightFieldCellSizeZ = TerrainData.mHeightFieldCellSizeZ;
    Desc.HeightFieldMaxHeight = TerrainData.mHeightFieldMaxHeight;
    Desc.HeightFieldCenterOrigin = TerrainData.mHeightFieldCenterOrigin;
    Desc.HeightFieldValues = TerrainData.mHeightFieldValues;
    Desc.mTerrainWorldData = std::make_shared<const TerrainWorldData>(TerrainData);
    return Desc;
}

PhysicsTerrainActor::ActorDesc PhysicsTerrainActor::BuildHeightFieldActorDesc(std::uint32_t HeightFieldWidth, std::uint32_t HeightFieldHeight, const std::vector<float>& HeightFieldValues, float HeightFieldMaxHeight, float HeightFieldCellSizeX, float HeightFieldCellSizeZ, bool HeightFieldCenterOrigin) {
    ActorDesc Desc{};
    Desc.HeightFieldWidth = HeightFieldWidth;
    Desc.HeightFieldHeight = HeightFieldHeight;
    Desc.HeightFieldCellSizeX = HeightFieldCellSizeX;
    Desc.HeightFieldCellSizeZ = HeightFieldCellSizeZ;
    Desc.HeightFieldMaxHeight = HeightFieldMaxHeight;
    Desc.HeightFieldCenterOrigin = HeightFieldCenterOrigin;
    Desc.HeightFieldValues = HeightFieldValues;

    if (HeightFieldWidth > 1U) {
        Desc.HalfExtentX = static_cast<float>(HeightFieldWidth - 1U) * HeightFieldCellSizeX * 0.5F;
    }

    if (HeightFieldHeight > 1U) {
        Desc.HalfExtentZ = static_cast<float>(HeightFieldHeight - 1U) * HeightFieldCellSizeZ * 0.5F;
    }

    Desc.Scale = DirectX::SimpleMath::Vector3{ 1.0F, 1.0F, 1.0F };
    Desc.mTerrainWorldData = std::make_shared<const TerrainWorldData>(BuildTerrainWorldDataFromActorDesc(Desc, 0U));
    return Desc;
}

bool PhysicsTerrainActor::IsTerrainActor() const {
    return true;
}

bool PhysicsTerrainActor::TryGetSurfaceHeightAtWorldPosition(float WorldX, float WorldZ, float& OutWorldHeight) const {
    DirectX::SimpleMath::Vector3 WorldNormal{ DirectX::SimpleMath::Vector3::Up };
    bool HasSurface{ TryGetSurfaceAtWorldPosition(WorldX, WorldZ, OutWorldHeight, WorldNormal) };
    return HasSurface;
}

bool PhysicsTerrainActor::TryGetSurfaceAtWorldPosition(float WorldX, float WorldZ, float& OutWorldHeight, DirectX::SimpleMath::Vector3& OutWorldNormal) const {
    const TerrainWorldData* TerrainData{ GetTerrainDataPointer() };
    if (TerrainData == nullptr) {
        return false;
    }

    bool HasSurface{ TryGetTerrainSurfaceAtWorldPosition(*TerrainData, WorldX, WorldZ, OutWorldHeight, OutWorldNormal) };
    return HasSurface;
}

bool PhysicsTerrainActor::TryRaycast(const DirectX::SimpleMath::Ray& Ray, float MaxDistance, DirectX::SimpleMath::Vector3& OutHitPosition, DirectX::SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) const {
    const TerrainWorldData* TerrainData{ GetTerrainDataPointer() };
    if (TerrainData == nullptr) {
        return false;
    }

    bool HasHit{ TryRaycastTerrainWorldData(*TerrainData, Ray, MaxDistance, OutHitPosition, OutHitNormal, OutHitDistance) };
    return HasHit;
}

bool PhysicsTerrainActor::ResolveDynamicCollision(PhysicsActorBase& DynamicActor, float DeltaTime) const {
    (void)DeltaTime;

    if (DynamicActor.GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic) {
        return false;
    }

    if (DynamicActor.HasFlag(PhysicsActorBase::PhysicsActorFlags::IgnoreTerrainCollide)) {
        return false;
    }

    float DynamicInverseMass{ DynamicActor.GetInverseMass() };
    if (DynamicInverseMass <= 0.0F) {
        return false;
    }

    const TerrainWorldData* TerrainData{ GetTerrainDataPointer() };
    if (TerrainData == nullptr || IsTerrainWorldDataValid(*TerrainData) == false) {
        return false;
    }

    const DirectX::SimpleMath::Vector3& Position{ TerrainData->mPosition };
    const DirectX::SimpleMath::Quaternion& Orientation{ TerrainData->mOrientation };
    const DirectX::SimpleMath::Vector3& Scale{ TerrainData->mScale };
    float TerrainHalfExtentX{ TerrainData->mHalfExtentX * std::abs(Scale.x) };
    float TerrainHalfExtentZ{ TerrainData->mHalfExtentZ * std::abs(Scale.z) };

    if (TerrainData->mHeightFieldWidth > 1U && TerrainData->mHeightFieldHeight > 1U && TerrainData->mHeightFieldCellSizeX > 0.0F && TerrainData->mHeightFieldCellSizeZ > 0.0F) {
        float HeightFieldHalfExtentX{ (static_cast<float>(TerrainData->mHeightFieldWidth - 1U) * TerrainData->mHeightFieldCellSizeX * 0.5F) * std::abs(Scale.x) };
        float HeightFieldHalfExtentZ{ (static_cast<float>(TerrainData->mHeightFieldHeight - 1U) * TerrainData->mHeightFieldCellSizeZ * 0.5F) * std::abs(Scale.z) };
        TerrainHalfExtentX = std::max(TerrainHalfExtentX, HeightFieldHalfExtentX);
        TerrainHalfExtentZ = std::max(TerrainHalfExtentZ, HeightFieldHalfExtentZ);
    }

    const DirectX::BoundingOrientedBox& PredictedWorldBoundingBox{ DynamicActor.GetWorldBoundingBox() };
    float TerrainHalfExtentY{ TerrainData->mHeightFieldMaxHeight * std::abs(Scale.y) + PredictedWorldBoundingBox.Extents.y };
    DirectX::BoundingOrientedBox TerrainBoundingBox{};
    TerrainBoundingBox.Center = DirectX::XMFLOAT3{ Position.x, Position.y + (TerrainHalfExtentY * 0.5F), Position.z };
    TerrainBoundingBox.Extents = DirectX::XMFLOAT3{ TerrainHalfExtentX, TerrainHalfExtentY, TerrainHalfExtentZ };
    TerrainBoundingBox.Orientation = DirectX::XMFLOAT4{ Orientation.x, Orientation.y, Orientation.z, Orientation.w };

    if (!TerrainBoundingBox.Intersects(PredictedWorldBoundingBox)) {
        return false;
    }

    DirectX::XMFLOAT3 DynamicCorners[8]{};
    PredictedWorldBoundingBox.GetCorners(DynamicCorners);
    float MaximumPenetrationDepth{};
    DirectX::SimpleMath::Vector3 ContactNormal{};
    DirectX::SimpleMath::Vector3 ContactPoint{};
    bool HasContact{};

    for (std::size_t CornerIndex{ 0U }; CornerIndex < 8U; ++CornerIndex) {
        DirectX::SimpleMath::Vector3 CornerWorldPosition{ DynamicCorners[CornerIndex].x, DynamicCorners[CornerIndex].y, DynamicCorners[CornerIndex].z };
        float SurfaceWorldHeight{};
        DirectX::SimpleMath::Vector3 SurfaceWorldNormal{ DirectX::SimpleMath::Vector3::Up };
        if (TryGetTerrainSurfaceAtWorldPosition(*TerrainData, CornerWorldPosition.x, CornerWorldPosition.z, SurfaceWorldHeight, SurfaceWorldNormal) == false) {
            continue;
        }

        float SurfaceWorldNormalLengthSquared{ SurfaceWorldNormal.LengthSquared() };
        if (SurfaceWorldNormalLengthSquared <= std::numeric_limits<float>::epsilon()) {
            continue;
        }

        SurfaceWorldNormal /= std::sqrt(SurfaceWorldNormalLengthSquared);
        DirectX::SimpleMath::Vector3 SurfaceWorldPosition{ CornerWorldPosition.x, SurfaceWorldHeight, CornerWorldPosition.z };
        DirectX::SimpleMath::Vector3 PenetrationVector{ SurfaceWorldPosition - CornerWorldPosition };
        float PenetrationDepth{ PenetrationVector.Dot(SurfaceWorldNormal) };
        if (PenetrationDepth <= 0.0F) {
            continue;
        }

        if (PenetrationDepth > MaximumPenetrationDepth) {
            MaximumPenetrationDepth = PenetrationDepth;
            ContactNormal = SurfaceWorldNormal;
            ContactPoint = SurfaceWorldPosition;
            HasContact = true;
        }
    }

    if (!HasContact) {
        return false;
    }

    DynamicActor.RegisterContactNormal(ContactNormal);
    ApplyTerrainContactImpulse(DynamicActor, ContactPoint, ContactNormal, MaximumPenetrationDepth, GetFriction(), DeltaTime);
    return true;
}

std::unique_ptr<PhysicsActorBase> PhysicsTerrainActor::Clone() const {
    std::unique_ptr<PhysicsActorBase> ClonedActor{ std::make_unique<PhysicsTerrainActor>(*this) };
    return ClonedActor;
}

const TerrainWorldData* PhysicsTerrainActor::GetTerrainDataPointer() const {
    return mTerrainWorldData.get();
}
