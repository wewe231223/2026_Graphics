#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

#include "PhysicsLib/Actors/PhysicsTerrainActor.h"

#undef min
#undef max

namespace {
    constexpr float RaycastDistanceEpsilon{ 1.0E-4F };
    constexpr float RaycastDeltaEpsilon{ 1.0E-4F };

    bool IsFiniteFloat(float Value) {
        return std::isfinite(Value) != 0;
    }

    bool IsFiniteVector3(const DirectX::SimpleMath::Vector3& Value) {
        return IsFiniteFloat(Value.x) && IsFiniteFloat(Value.y) && IsFiniteFloat(Value.z);
    }

    bool TryResolveTerrainRaySample(const PhysicsTerrainActor& TerrainActor, const DirectX::SimpleMath::Ray& Ray, float RayDistance, float& OutTerrainDelta, DirectX::SimpleMath::Vector3& OutSurfacePosition, DirectX::SimpleMath::Vector3& OutSurfaceNormal) {
        const DirectX::SimpleMath::Vector3 RayPosition{ Ray.position + (Ray.direction * RayDistance) };
        if (IsFiniteVector3(RayPosition) == false) {
            return false;
        }

        float SurfaceHeight{};
        DirectX::SimpleMath::Vector3 SurfaceNormal{ DirectX::SimpleMath::Vector3::Up };
        if (TerrainActor.TryGetSurfaceAtWorldPosition(RayPosition.x, RayPosition.z, SurfaceHeight, SurfaceNormal) == false || IsFiniteFloat(SurfaceHeight) == false || IsFiniteVector3(SurfaceNormal) == false) {
            return false;
        }

        const float TerrainDelta{ RayPosition.y - SurfaceHeight };
        if (IsFiniteFloat(TerrainDelta) == false) {
            return false;
        }

        OutTerrainDelta = TerrainDelta;
        OutSurfacePosition = DirectX::SimpleMath::Vector3{ RayPosition.x, SurfaceHeight, RayPosition.z };
        OutSurfaceNormal = SurfaceNormal;
        return true;
    }
}

PhysicsTerrainActor::PhysicsTerrainActor()
    : PhysicsStaticActor{},
      mHalfExtentX{ 0.5F },
      mHalfExtentZ{ 0.5F },
      mHeightFieldWidth{},
      mHeightFieldHeight{},
      mHeightFieldCellSizeX{ 1.0F },
      mHeightFieldCellSizeZ{ 1.0F },
      mHeightFieldMaxHeight{ 1.0F },
      mHeightFieldCenterOrigin{ true },
      mHeightFieldValues{} {
}

PhysicsTerrainActor::~PhysicsTerrainActor() {
}

PhysicsTerrainActor::PhysicsTerrainActor(const PhysicsTerrainActor& Other)
    : PhysicsStaticActor{ Other },
      mHalfExtentX{ Other.mHalfExtentX },
      mHalfExtentZ{ Other.mHalfExtentZ },
      mHeightFieldWidth{ Other.mHeightFieldWidth },
      mHeightFieldHeight{ Other.mHeightFieldHeight },
      mHeightFieldCellSizeX{ Other.mHeightFieldCellSizeX },
      mHeightFieldCellSizeZ{ Other.mHeightFieldCellSizeZ },
      mHeightFieldMaxHeight{ Other.mHeightFieldMaxHeight },
      mHeightFieldCenterOrigin{ Other.mHeightFieldCenterOrigin },
      mHeightFieldValues{ Other.mHeightFieldValues } {
}

PhysicsTerrainActor& PhysicsTerrainActor::operator=(const PhysicsTerrainActor& Other) {
    if (this == &Other) {
        return *this;
    }

    PhysicsStaticActor::operator=(Other);
    mHalfExtentX = Other.mHalfExtentX;
    mHalfExtentZ = Other.mHalfExtentZ;
    mHeightFieldWidth = Other.mHeightFieldWidth;
    mHeightFieldHeight = Other.mHeightFieldHeight;
    mHeightFieldCellSizeX = Other.mHeightFieldCellSizeX;
    mHeightFieldCellSizeZ = Other.mHeightFieldCellSizeZ;
    mHeightFieldMaxHeight = Other.mHeightFieldMaxHeight;
    mHeightFieldCenterOrigin = Other.mHeightFieldCenterOrigin;
    mHeightFieldValues = Other.mHeightFieldValues;

    return *this;
}

PhysicsTerrainActor::PhysicsTerrainActor(PhysicsTerrainActor&& Other) noexcept
    : PhysicsStaticActor{ std::move(Other) },
      mHalfExtentX{ Other.mHalfExtentX },
      mHalfExtentZ{ Other.mHalfExtentZ },
      mHeightFieldWidth{ Other.mHeightFieldWidth },
      mHeightFieldHeight{ Other.mHeightFieldHeight },
      mHeightFieldCellSizeX{ Other.mHeightFieldCellSizeX },
      mHeightFieldCellSizeZ{ Other.mHeightFieldCellSizeZ },
      mHeightFieldMaxHeight{ Other.mHeightFieldMaxHeight },
      mHeightFieldCenterOrigin{ Other.mHeightFieldCenterOrigin },
      mHeightFieldValues{ std::move(Other.mHeightFieldValues) } {
}

PhysicsTerrainActor& PhysicsTerrainActor::operator=(PhysicsTerrainActor&& Other) noexcept {
    if (this == &Other) {
        return *this;
    }

    PhysicsStaticActor::operator=(std::move(Other));
    mHalfExtentX = Other.mHalfExtentX;
    mHalfExtentZ = Other.mHalfExtentZ;
    mHeightFieldWidth = Other.mHeightFieldWidth;
    mHeightFieldHeight = Other.mHeightFieldHeight;
    mHeightFieldCellSizeX = Other.mHeightFieldCellSizeX;
    mHeightFieldCellSizeZ = Other.mHeightFieldCellSizeZ;
    mHeightFieldMaxHeight = Other.mHeightFieldMaxHeight;
    mHeightFieldCenterOrigin = Other.mHeightFieldCenterOrigin;
    mHeightFieldValues = std::move(Other.mHeightFieldValues);

    return *this;
}

PhysicsTerrainActor::PhysicsTerrainActor(const ActorDesc& Desc)
    : PhysicsStaticActor{},
      mHalfExtentX{ Desc.HalfExtentX },
      mHalfExtentZ{ Desc.HalfExtentZ },
      mHeightFieldWidth{ Desc.HeightFieldWidth },
      mHeightFieldHeight{ Desc.HeightFieldHeight },
      mHeightFieldCellSizeX{ Desc.HeightFieldCellSizeX },
      mHeightFieldCellSizeZ{ Desc.HeightFieldCellSizeZ },
      mHeightFieldMaxHeight{ Desc.HeightFieldMaxHeight },
      mHeightFieldCenterOrigin{ Desc.HeightFieldCenterOrigin },
      mHeightFieldValues{ Desc.HeightFieldValues } {
    SetPosition(Desc.Position);
    SetRotation(Desc.Rotation);
    SetScale(Desc.Scale);
}

void PhysicsTerrainActor::SetActorDesc(const ActorDesc& Desc) {
    SetPosition(Desc.Position);
    SetRotation(Desc.Rotation);
    SetScale(Desc.Scale);
    mHalfExtentX = Desc.HalfExtentX;
    mHalfExtentZ = Desc.HalfExtentZ;
    mHeightFieldWidth = Desc.HeightFieldWidth;
    mHeightFieldHeight = Desc.HeightFieldHeight;
    mHeightFieldCellSizeX = Desc.HeightFieldCellSizeX;
    mHeightFieldCellSizeZ = Desc.HeightFieldCellSizeZ;
    mHeightFieldMaxHeight = Desc.HeightFieldMaxHeight;
    mHeightFieldCenterOrigin = Desc.HeightFieldCenterOrigin;
    mHeightFieldValues = Desc.HeightFieldValues;
}

PhysicsTerrainActor::ActorDesc PhysicsTerrainActor::GetActorDesc() const {
    ActorDesc Desc{};
    Desc.Position = GetPosition();
    Desc.Rotation = GetRotation();
    Desc.Scale = GetScale();
    Desc.HalfExtentX = mHalfExtentX;
    Desc.HalfExtentZ = mHalfExtentZ;
    Desc.HeightFieldWidth = mHeightFieldWidth;
    Desc.HeightFieldHeight = mHeightFieldHeight;
    Desc.HeightFieldCellSizeX = mHeightFieldCellSizeX;
    Desc.HeightFieldCellSizeZ = mHeightFieldCellSizeZ;
    Desc.HeightFieldMaxHeight = mHeightFieldMaxHeight;
    Desc.HeightFieldCenterOrigin = mHeightFieldCenterOrigin;
    Desc.HeightFieldValues = mHeightFieldValues;
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

    return Desc;
}

bool PhysicsTerrainActor::TryGetSurfaceHeightAtWorldPosition(float WorldX, float WorldZ, float& OutWorldHeight) const {
    DirectX::SimpleMath::Vector3 WorldNormal{ DirectX::SimpleMath::Vector3::Up };
    bool HasSurface{ TryGetSurfaceAtWorldPosition(WorldX, WorldZ, OutWorldHeight, WorldNormal) };
    return HasSurface;
}

bool PhysicsTerrainActor::TryGetSurfaceAtWorldPosition(float WorldX, float WorldZ, float& OutWorldHeight, DirectX::SimpleMath::Vector3& OutWorldNormal) const {
    const DirectX::SimpleMath::Vector3& Position{ GetPosition() };
    const DirectX::SimpleMath::Quaternion& Orientation{ GetOrientation() };
    const DirectX::SimpleMath::Vector3& Scale{ GetScale() };
    DirectX::SimpleMath::Matrix ScalingMatrix{ DirectX::SimpleMath::Matrix::CreateScale(Scale) };
    DirectX::SimpleMath::Matrix RotationMatrix{ DirectX::SimpleMath::Matrix::CreateFromQuaternion(Orientation) };
    DirectX::SimpleMath::Matrix TranslationMatrix{ DirectX::SimpleMath::Matrix::CreateTranslation(Position) };
    DirectX::SimpleMath::Matrix WorldMatrix{ ScalingMatrix * RotationMatrix * TranslationMatrix };
    DirectX::SimpleMath::Matrix InverseWorldMatrix{ WorldMatrix.Invert() };
    DirectX::SimpleMath::Matrix InverseTransposeWorldMatrix{ InverseWorldMatrix.Transpose() };
    DirectX::SimpleMath::Vector3 LocalPoint{ DirectX::SimpleMath::Vector3::Transform(DirectX::SimpleMath::Vector3{ WorldX, 0.0F, WorldZ }, InverseWorldMatrix) };

    float LocalHeight{};
    DirectX::SimpleMath::Vector3 LocalNormal{ DirectX::SimpleMath::Vector3::Up };
    bool HasLocalSurface{ TryResolveSurfaceAtLocalPosition(LocalPoint.x, LocalPoint.z, LocalHeight, LocalNormal) };
    if (!HasLocalSurface) {
        return false;
    }

    DirectX::SimpleMath::Vector3 WorldPoint{ DirectX::SimpleMath::Vector3::Transform(DirectX::SimpleMath::Vector3{ LocalPoint.x, LocalHeight, LocalPoint.z }, WorldMatrix) };
    DirectX::SimpleMath::Vector3 WorldNormal{ DirectX::SimpleMath::Vector3::TransformNormal(LocalNormal, InverseTransposeWorldMatrix) };
    const float WorldNormalLengthSquared{ WorldNormal.LengthSquared() };
    if (WorldNormalLengthSquared <= std::numeric_limits<float>::epsilon()) {
        WorldNormal = DirectX::SimpleMath::Vector3::Up;
    }
    else {
        WorldNormal /= std::sqrt(WorldNormalLengthSquared);
    }

    OutWorldHeight = WorldPoint.y;
    OutWorldNormal = WorldNormal;
    return true;
}

bool PhysicsTerrainActor::TryRaycast(const DirectX::SimpleMath::Ray& Ray, float MaxDistance, DirectX::SimpleMath::Vector3& OutHitPosition, DirectX::SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) const {
    if (IsFiniteVector3(Ray.position) == false || IsFiniteVector3(Ray.direction) == false || IsFiniteFloat(MaxDistance) == false) {
        return false;
    }

    DirectX::SimpleMath::Vector3 SafeRayDirection{ Ray.direction };
    const float RayDirectionLengthSquared{ SafeRayDirection.LengthSquared() };
    if (IsFiniteFloat(RayDirectionLengthSquared) == false || RayDirectionLengthSquared <= RaycastDistanceEpsilon) {
        return false;
    }

    SafeRayDirection.Normalize();
    if (IsFiniteVector3(SafeRayDirection) == false) {
        return false;
    }

    const float SafeMaxDistance{ std::max(MaxDistance, 0.0F) };
    if (SafeMaxDistance <= RaycastDistanceEpsilon) {
        return false;
    }

    const DirectX::SimpleMath::Ray SafeRay{ Ray.position, SafeRayDirection };
    const DirectX::SimpleMath::Vector3& Scale{ GetScale() };
    const float WorldCellSizeX{ std::abs(mHeightFieldCellSizeX * Scale.x) };
    const float WorldCellSizeZ{ std::abs(mHeightFieldCellSizeZ * Scale.z) };
    const float MinimumCellSize{ std::min(WorldCellSizeX, WorldCellSizeZ) };
    const float SampleStepDistance{ std::max(MinimumCellSize * 0.5F, 0.05F) };
    if (IsFiniteFloat(SampleStepDistance) == false || SampleStepDistance <= 0.0F) {
        return false;
    }

    const std::uint32_t SampleStepCount{ std::max(static_cast<std::uint32_t>(1U), static_cast<std::uint32_t>(std::ceil(SafeMaxDistance / SampleStepDistance))) };
    bool IsPreviousSampleResolved{};
    float PreviousSampleDistance{};
    float PreviousSampleDelta{};
    DirectX::SimpleMath::Vector3 PreviousSurfacePosition{};
    DirectX::SimpleMath::Vector3 PreviousSurfaceNormal{ DirectX::SimpleMath::Vector3::Up };

    if (TryResolveTerrainRaySample(*this, SafeRay, 0.0F, PreviousSampleDelta, PreviousSurfacePosition, PreviousSurfaceNormal) == true) {
        if (PreviousSampleDelta <= RaycastDeltaEpsilon) {
            OutHitPosition = PreviousSurfacePosition;
            OutHitNormal = PreviousSurfaceNormal;
            OutHitDistance = 0.0F;
            return true;
        }

        IsPreviousSampleResolved = true;
    }

    for (std::uint32_t SampleIndex{ 1U }; SampleIndex <= SampleStepCount; ++SampleIndex) {
        const float CurrentSampleDistance{ std::min(SafeMaxDistance, static_cast<float>(SampleIndex) * SampleStepDistance) };
        float CurrentSampleDelta{};
        DirectX::SimpleMath::Vector3 CurrentSurfacePosition{};
        DirectX::SimpleMath::Vector3 CurrentSurfaceNormal{ DirectX::SimpleMath::Vector3::Up };
        const bool IsCurrentSampleResolved{ TryResolveTerrainRaySample(*this, SafeRay, CurrentSampleDistance, CurrentSampleDelta, CurrentSurfacePosition, CurrentSurfaceNormal) };

        if (IsPreviousSampleResolved == true && IsCurrentSampleResolved == true) {
            const bool IsCrossedSurface{ (PreviousSampleDelta > 0.0F && CurrentSampleDelta <= 0.0F) || (PreviousSampleDelta < 0.0F && CurrentSampleDelta >= 0.0F) || std::abs(CurrentSampleDelta) <= RaycastDeltaEpsilon };
            if (IsCrossedSurface == true) {
                float HitDistance{ CurrentSampleDistance };
                const float DeltaDifference{ CurrentSampleDelta - PreviousSampleDelta };
                if (std::abs(DeltaDifference) > RaycastDistanceEpsilon) {
                    const float HitAlpha{ std::clamp(PreviousSampleDelta / (PreviousSampleDelta - CurrentSampleDelta), 0.0F, 1.0F) };
                    HitDistance = PreviousSampleDistance + ((CurrentSampleDistance - PreviousSampleDistance) * HitAlpha);
                }

                float HitSampleDelta{};
                DirectX::SimpleMath::Vector3 HitSurfacePosition{};
                DirectX::SimpleMath::Vector3 HitSurfaceNormal{ DirectX::SimpleMath::Vector3::Up };
                if (TryResolveTerrainRaySample(*this, SafeRay, HitDistance, HitSampleDelta, HitSurfacePosition, HitSurfaceNormal) == false) {
                    HitDistance = CurrentSampleDistance;
                    HitSurfacePosition = CurrentSurfacePosition;
                    HitSurfaceNormal = CurrentSurfaceNormal;
                }

                OutHitPosition = HitSurfacePosition;
                OutHitNormal = HitSurfaceNormal;
                OutHitDistance = HitDistance;
                return true;
            }
        }

        if (IsCurrentSampleResolved == true) {
            IsPreviousSampleResolved = true;
            PreviousSampleDistance = CurrentSampleDistance;
            PreviousSampleDelta = CurrentSampleDelta;
            PreviousSurfacePosition = CurrentSurfacePosition;
            PreviousSurfaceNormal = CurrentSurfaceNormal;
        }
    }

    return false;
}

bool PhysicsTerrainActor::ResolveDynamicCollision(PhysicsActorBase& DynamicActor, float DeltaTime) const {
    (void)DeltaTime;

    if (DynamicActor.GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic) {
        return false;
    }

    if (!DynamicActor.HasFlag(PhysicsActorBase::PhysicsActorFlags::TerrainCollide)) {
        return false;
    }

    float DynamicInverseMass{ DynamicActor.GetInverseMass() };
    if (DynamicInverseMass <= 0.0F) {
        return false;
    }

    const DirectX::SimpleMath::Vector3& Position{ GetPosition() };
    const DirectX::SimpleMath::Quaternion& Orientation{ GetOrientation() };
    const DirectX::SimpleMath::Vector3& Scale{ GetScale() };
    float TerrainHalfExtentX{ mHalfExtentX * std::abs(Scale.x) };
    float TerrainHalfExtentZ{ mHalfExtentZ * std::abs(Scale.z) };

    if (mHeightFieldWidth > 1U && mHeightFieldHeight > 1U && mHeightFieldCellSizeX > 0.0F && mHeightFieldCellSizeZ > 0.0F) {
        float HeightFieldHalfExtentX{ (static_cast<float>(mHeightFieldWidth - 1U) * mHeightFieldCellSizeX * 0.5F) * std::abs(Scale.x) };
        float HeightFieldHalfExtentZ{ (static_cast<float>(mHeightFieldHeight - 1U) * mHeightFieldCellSizeZ * 0.5F) * std::abs(Scale.z) };
        TerrainHalfExtentX = std::max(TerrainHalfExtentX, HeightFieldHalfExtentX);
        TerrainHalfExtentZ = std::max(TerrainHalfExtentZ, HeightFieldHalfExtentZ);
    }

    const DirectX::BoundingOrientedBox& PredictedWorldBoundingBox{ DynamicActor.GetWorldBoundingBox() };
    float TerrainHalfExtentY{ mHeightFieldMaxHeight * std::abs(Scale.y) + PredictedWorldBoundingBox.Extents.y };
    DirectX::BoundingOrientedBox TerrainBoundingBox{};
    TerrainBoundingBox.Center = DirectX::XMFLOAT3{ Position.x, Position.y + (TerrainHalfExtentY * 0.5F), Position.z };
    TerrainBoundingBox.Extents = DirectX::XMFLOAT3{ TerrainHalfExtentX, TerrainHalfExtentY, TerrainHalfExtentZ };
    TerrainBoundingBox.Orientation = DirectX::XMFLOAT4{ Orientation.x, Orientation.y, Orientation.z, Orientation.w };

    if (!TerrainBoundingBox.Intersects(PredictedWorldBoundingBox)) {
        return false;
    }

    DirectX::SimpleMath::Matrix TerrainScalingMatrix{ DirectX::SimpleMath::Matrix::CreateScale(Scale) };
    DirectX::SimpleMath::Matrix TerrainRotationMatrix{ DirectX::SimpleMath::Matrix::CreateFromQuaternion(Orientation) };
    DirectX::SimpleMath::Matrix TerrainTranslationMatrix{ DirectX::SimpleMath::Matrix::CreateTranslation(Position) };
    DirectX::SimpleMath::Matrix TerrainWorldMatrix{ TerrainScalingMatrix * TerrainRotationMatrix * TerrainTranslationMatrix };
    DirectX::SimpleMath::Matrix InverseTerrainWorldMatrix{ TerrainWorldMatrix.Invert() };
    DirectX::SimpleMath::Matrix InverseTransposeTerrainWorldMatrix{ InverseTerrainWorldMatrix.Transpose() };
    DirectX::XMFLOAT3 DynamicCorners[8]{};
    PredictedWorldBoundingBox.GetCorners(DynamicCorners);
    float MaximumPenetrationDepth{};
    DirectX::SimpleMath::Vector3 ContactNormal{};
    bool HasContact{};

    for (std::size_t CornerIndex{ 0U }; CornerIndex < 8U; ++CornerIndex) {
        DirectX::SimpleMath::Vector3 CornerWorldPosition{ DynamicCorners[CornerIndex].x, DynamicCorners[CornerIndex].y, DynamicCorners[CornerIndex].z };
        DirectX::SimpleMath::Vector3 CornerLocalPosition{ DirectX::SimpleMath::Vector3::Transform(CornerWorldPosition, InverseTerrainWorldMatrix) };
        float SurfaceLocalHeight{};
        if (!TryGetSurfaceHeightAtLocalPosition(CornerLocalPosition.x, CornerLocalPosition.z, SurfaceLocalHeight)) {
            continue;
        }

        DirectX::SimpleMath::Vector3 SurfaceLocalNormal{};
        if (!TryGetSurfaceNormalAtLocalPosition(CornerLocalPosition.x, CornerLocalPosition.z, SurfaceLocalNormal)) {
            continue;
        }

        DirectX::SimpleMath::Vector3 SurfaceWorldPosition{ DirectX::SimpleMath::Vector3::Transform(DirectX::SimpleMath::Vector3{ CornerLocalPosition.x, SurfaceLocalHeight, CornerLocalPosition.z }, TerrainWorldMatrix) };
        DirectX::SimpleMath::Vector3 SurfaceWorldNormal{ DirectX::SimpleMath::Vector3::TransformNormal(SurfaceLocalNormal, InverseTransposeTerrainWorldMatrix) };
        float SurfaceWorldNormalLengthSquared{ SurfaceWorldNormal.LengthSquared() };
        if (SurfaceWorldNormalLengthSquared <= std::numeric_limits<float>::epsilon()) {
            continue;
        }

        SurfaceWorldNormal /= std::sqrt(SurfaceWorldNormalLengthSquared);
        DirectX::SimpleMath::Vector3 PenetrationVector{ SurfaceWorldPosition - CornerWorldPosition };
        float PenetrationDepth{ PenetrationVector.Dot(SurfaceWorldNormal) };
        if (PenetrationDepth <= 0.0F) {
            continue;
        }

        if (PenetrationDepth > MaximumPenetrationDepth) {
            MaximumPenetrationDepth = PenetrationDepth;
            ContactNormal = SurfaceWorldNormal;
            HasContact = true;
        }
    }

    if (!HasContact) {
        return false;
    }

    DirectX::SimpleMath::Vector3 CorrectedPosition{ DynamicActor.GetPosition() };
    DirectX::SimpleMath::Vector3 CorrectedVelocity{ DynamicActor.GetVelocity() };

    CorrectedPosition += ContactNormal * MaximumPenetrationDepth;
    float VelocityProjection{ CorrectedVelocity.Dot(ContactNormal) };
    if (VelocityProjection < 0.0F) {
        float DynamicRestitution{ DynamicActor.GetRestitution() };
        float EffectiveRestitution{ std::clamp(DynamicRestitution, 0.0F, 1.0F) };
        DirectX::SimpleMath::Vector3 LinearMomentum{ CorrectedVelocity / DynamicInverseMass };
        float NormalImpulseMagnitude{ -(1.0F + EffectiveRestitution) * VelocityProjection / DynamicInverseMass };
        DirectX::SimpleMath::Vector3 NormalImpulse{ ContactNormal * NormalImpulseMagnitude };
        LinearMomentum += NormalImpulse;

        DirectX::SimpleMath::Vector3 VelocityAfterNormal{ LinearMomentum * DynamicInverseMass };
        DirectX::SimpleMath::Vector3 TangentialVelocity{ VelocityAfterNormal - (ContactNormal * VelocityAfterNormal.Dot(ContactNormal)) };
        float TangentialVelocityLength{ TangentialVelocity.Length() };
        if (TangentialVelocityLength > 0.0001F) {
            DirectX::SimpleMath::Vector3 Tangent{ TangentialVelocity / TangentialVelocityLength };
            float DynamicFriction{ DynamicActor.GetFriction() };
            float EffectiveFriction{ std::sqrt(std::max(0.0F, DynamicFriction * GetFriction())) };
            float FrictionImpulseMagnitude{ -VelocityAfterNormal.Dot(Tangent) / DynamicInverseMass };
            float MaximumFrictionImpulse{ std::abs(NormalImpulseMagnitude) * EffectiveFriction };
            FrictionImpulseMagnitude = std::clamp(FrictionImpulseMagnitude, -MaximumFrictionImpulse, MaximumFrictionImpulse);
            LinearMomentum += Tangent * FrictionImpulseMagnitude;
        }

        CorrectedVelocity = LinearMomentum * DynamicInverseMass;
    }

    DynamicActor.SetPosition(CorrectedPosition);
    DynamicActor.SetVelocity(CorrectedVelocity);

    return true;
}

std::unique_ptr<PhysicsActorBase> PhysicsTerrainActor::Clone() const {
    std::unique_ptr<PhysicsActorBase> ClonedActor{ std::make_unique<PhysicsTerrainActor>(*this) };
    return ClonedActor;
}

bool PhysicsTerrainActor::TryGetSurfaceHeightAtLocalPosition(float LocalX, float LocalZ, float& OutLocalHeight) const {
    DirectX::SimpleMath::Vector3 LocalNormal{ DirectX::SimpleMath::Vector3::Up };
    bool HasSurface{ TryResolveSurfaceAtLocalPosition(LocalX, LocalZ, OutLocalHeight, LocalNormal) };
    return HasSurface;
}

bool PhysicsTerrainActor::TryGetSurfaceNormalAtLocalPosition(float LocalX, float LocalZ, DirectX::SimpleMath::Vector3& OutLocalNormal) const {
    float LocalHeight{};
    bool HasSurface{ TryResolveSurfaceAtLocalPosition(LocalX, LocalZ, LocalHeight, OutLocalNormal) };
    return HasSurface;
}

bool PhysicsTerrainActor::TryResolveSurfaceAtLocalPosition(float LocalX, float LocalZ, float& OutLocalHeight, DirectX::SimpleMath::Vector3& OutLocalNormal) const {
    const std::size_t ExpectedHeightValueCount{ static_cast<std::size_t>(mHeightFieldWidth) * static_cast<std::size_t>(mHeightFieldHeight) };
    if (mHeightFieldWidth < 2U || mHeightFieldHeight < 2U || mHeightFieldValues.size() != ExpectedHeightValueCount) {
        return false;
    }

    if (mHeightFieldCellSizeX <= 0.0F || mHeightFieldCellSizeZ <= 0.0F || mHeightFieldMaxHeight <= 0.0F) {
        return false;
    }

    float GridPositionX{ LocalX };
    float GridPositionZ{ LocalZ };
    if (mHeightFieldCenterOrigin) {
        GridPositionX += (static_cast<float>(mHeightFieldWidth) - 1.0F) * mHeightFieldCellSizeX * 0.5F;
        GridPositionZ += (static_cast<float>(mHeightFieldHeight) - 1.0F) * mHeightFieldCellSizeZ * 0.5F;
    }

    const float MaxGridPositionX{ static_cast<float>(mHeightFieldWidth - 1U) * mHeightFieldCellSizeX };
    const float MaxGridPositionZ{ static_cast<float>(mHeightFieldHeight - 1U) * mHeightFieldCellSizeZ };
    if (GridPositionX < 0.0F || GridPositionZ < 0.0F || GridPositionX > MaxGridPositionX || GridPositionZ > MaxGridPositionZ) {
        return false;
    }

    const float GridX{ GridPositionX / mHeightFieldCellSizeX };
    const float GridZ{ GridPositionZ / mHeightFieldCellSizeZ };
    const std::uint32_t BaseGridX{ std::min(static_cast<std::uint32_t>(std::floor(GridX)), mHeightFieldWidth - 2U) };
    const std::uint32_t BaseGridZ{ std::min(static_cast<std::uint32_t>(std::floor(GridZ)), mHeightFieldHeight - 2U) };
    const std::uint32_t NextGridX{ BaseGridX + 1U };
    const std::uint32_t NextGridZ{ BaseGridZ + 1U };

    const float LocalGridX{ GridX - static_cast<float>(BaseGridX) };
    const float LocalGridZ{ GridZ - static_cast<float>(BaseGridZ) };

    const float Height00{ SampleCellHeight(BaseGridX, BaseGridZ) };
    const float Height10{ SampleCellHeight(NextGridX, BaseGridZ) };
    const float Height01{ SampleCellHeight(BaseGridX, NextGridZ) };
    const float Height11{ SampleCellHeight(NextGridX, NextGridZ) };
    const float HeightTop{ Height00 + ((Height10 - Height00) * LocalGridX) };
    const float HeightBottom{ Height01 + ((Height11 - Height01) * LocalGridX) };
    const float InterpolatedHeight{ HeightTop + ((HeightBottom - HeightTop) * LocalGridZ) };

    const float HeightDeltaX0{ Height10 - Height00 };
    const float HeightDeltaX1{ Height11 - Height01 };
    const float HeightDeltaZ0{ Height01 - Height00 };
    const float HeightDeltaZ1{ Height11 - Height10 };
    const float HeightDerivativeX{ ((1.0F - LocalGridZ) * HeightDeltaX0) + (LocalGridZ * HeightDeltaX1) };
    const float HeightDerivativeZ{ ((1.0F - LocalGridX) * HeightDeltaZ0) + (LocalGridX * HeightDeltaZ1) };
    const float SlopeX{ HeightDerivativeX / mHeightFieldCellSizeX };
    const float SlopeZ{ HeightDerivativeZ / mHeightFieldCellSizeZ };
    DirectX::SimpleMath::Vector3 SurfaceNormal{ -SlopeX, 1.0F, -SlopeZ };
    const float SurfaceNormalLengthSquared{ SurfaceNormal.LengthSquared() };
    if (SurfaceNormalLengthSquared <= 0.0F) {
        SurfaceNormal = DirectX::SimpleMath::Vector3::Up;
    }
    else {
        SurfaceNormal.Normalize();
    }

    OutLocalHeight = InterpolatedHeight;
    OutLocalNormal = SurfaceNormal;
    return true;
}

std::size_t PhysicsTerrainActor::CalculateHeightFieldIndex(std::uint32_t X, std::uint32_t Z) const {
    std::size_t Index{ static_cast<std::size_t>(Z) * static_cast<std::size_t>(mHeightFieldWidth) + static_cast<std::size_t>(X) };
    return Index;
}

float PhysicsTerrainActor::SampleCellHeight(std::uint32_t X, std::uint32_t Z) const {
    const std::size_t HeightFieldIndex{ CalculateHeightFieldIndex(X, Z) };
    const float Height01Value{ std::clamp(mHeightFieldValues[HeightFieldIndex], 0.0F, 1.0F) };
    return Height01Value * mHeightFieldMaxHeight;
}
