#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

#include "PhysicsLib/Actors/PhysicsTerrainActor.h"
#include "Utility/MathValidation.h"

#undef min
#undef max

namespace {
    constexpr float TerrainContactEpsilon{ 1.0E-4F };
    constexpr float TerrainRestitutionThreshold{ 1.25F };
    constexpr float TerrainPenetrationVelocityFactor{ 0.04F };
    constexpr float TerrainMaximumBiasVelocity{ 0.5F };
    constexpr float TerrainPositionCorrectionFactor{ 1.0F };
    constexpr float TerrainPositionCorrectionSlop{ 0.0005F };
    constexpr float TerrainRaycastDistanceEpsilon{ 1.0E-4F };
    constexpr float TerrainRaycastDeltaEpsilon{ 1.0E-4F };

    bool IsPhysicsTerrainDataHandleValid(Terrain::TerrainDataHandle Handle) {
        return Handle.mValue != (std::numeric_limits<std::uint32_t>::max)() && Handle.mGeneration != 0U;
    }

    DirectX::SimpleMath::Matrix BuildTerrainWorldMatrix(const Terrain::TerrainWorldData& TerrainData) {
        DirectX::SimpleMath::Matrix ScalingMatrix{ DirectX::SimpleMath::Matrix::CreateScale(TerrainData.mScale) };
        DirectX::SimpleMath::Matrix RotationMatrix{ DirectX::SimpleMath::Matrix::CreateFromQuaternion(TerrainData.mOrientation) };
        DirectX::SimpleMath::Matrix TranslationMatrix{ DirectX::SimpleMath::Matrix::CreateTranslation(TerrainData.mPosition) };
        DirectX::SimpleMath::Matrix WorldMatrix{ ScalingMatrix * RotationMatrix * TranslationMatrix };
        return WorldMatrix;
    }

    bool IsTerrainWorldDataValid(const Terrain::TerrainWorldData& TerrainData) {
        if (TerrainData.mHeightFieldValues == nullptr) {
            return false;
        }

        const std::size_t ExpectedHeightValueCount{ static_cast<std::size_t>(TerrainData.mHeightFieldWidth) * static_cast<std::size_t>(TerrainData.mHeightFieldHeight) };
        return TerrainData.mHeightFieldWidth > 1U && TerrainData.mHeightFieldHeight > 1U && TerrainData.mHeightFieldCellSizeX > 0.0F && TerrainData.mHeightFieldCellSizeZ > 0.0F && TerrainData.mHeightFieldMaxHeight > 0.0F && TerrainData.mHeightFieldValues->size() == ExpectedHeightValueCount;
    }

    std::size_t CalculateHeightFieldIndex(const Terrain::TerrainWorldData& TerrainData, std::uint32_t X, std::uint32_t Z) {
        const std::size_t Index{ static_cast<std::size_t>(Z) * static_cast<std::size_t>(TerrainData.mHeightFieldWidth) + static_cast<std::size_t>(X) };
        return Index;
    }

    float SampleCellHeight(const Terrain::TerrainWorldData& TerrainData, std::uint32_t X, std::uint32_t Z) {
        const std::size_t HeightFieldIndex{ CalculateHeightFieldIndex(TerrainData, X, Z) };
        const float Height01Value{ std::clamp((*TerrainData.mHeightFieldValues)[HeightFieldIndex], 0.0F, 1.0F) };
        return Height01Value * TerrainData.mHeightFieldMaxHeight;
    }

    bool TryResolveTerrainSurfaceAtLocalPosition(const Terrain::TerrainWorldData& TerrainData, float LocalX, float LocalZ, float& OutLocalHeight, DirectX::SimpleMath::Vector3& OutLocalNormal) {
        if (IsTerrainWorldDataValid(TerrainData) == false) {
            return false;
        }

        float GridPositionX{ LocalX };
        float GridPositionZ{ LocalZ };
        if (TerrainData.mHeightFieldCenterOrigin == true) {
            GridPositionX += (static_cast<float>(TerrainData.mHeightFieldWidth) - 1.0F) * TerrainData.mHeightFieldCellSizeX * 0.5F;
            GridPositionZ += (static_cast<float>(TerrainData.mHeightFieldHeight) - 1.0F) * TerrainData.mHeightFieldCellSizeZ * 0.5F;
        }

        const float MaxGridPositionX{ static_cast<float>(TerrainData.mHeightFieldWidth - 1U) * TerrainData.mHeightFieldCellSizeX };
        const float MaxGridPositionZ{ static_cast<float>(TerrainData.mHeightFieldHeight - 1U) * TerrainData.mHeightFieldCellSizeZ };
        if (GridPositionX < 0.0F || GridPositionZ < 0.0F || GridPositionX > MaxGridPositionX || GridPositionZ > MaxGridPositionZ) {
            return false;
        }

        const float GridX{ GridPositionX / TerrainData.mHeightFieldCellSizeX };
        const float GridZ{ GridPositionZ / TerrainData.mHeightFieldCellSizeZ };
        const std::uint32_t BaseGridX{ std::min(static_cast<std::uint32_t>(std::floor(GridX)), TerrainData.mHeightFieldWidth - 2U) };
        const std::uint32_t BaseGridZ{ std::min(static_cast<std::uint32_t>(std::floor(GridZ)), TerrainData.mHeightFieldHeight - 2U) };
        const std::uint32_t NextGridX{ BaseGridX + 1U };
        const std::uint32_t NextGridZ{ BaseGridZ + 1U };

        const float LocalGridX{ GridX - static_cast<float>(BaseGridX) };
        const float LocalGridZ{ GridZ - static_cast<float>(BaseGridZ) };
        const float Height00{ SampleCellHeight(TerrainData, BaseGridX, BaseGridZ) };
        const float Height10{ SampleCellHeight(TerrainData, NextGridX, BaseGridZ) };
        const float Height01{ SampleCellHeight(TerrainData, BaseGridX, NextGridZ) };
        const float Height11{ SampleCellHeight(TerrainData, NextGridX, NextGridZ) };
        const float HeightTop{ Height00 + ((Height10 - Height00) * LocalGridX) };
        const float HeightBottom{ Height01 + ((Height11 - Height01) * LocalGridX) };
        const float InterpolatedHeight{ HeightTop + ((HeightBottom - HeightTop) * LocalGridZ) };
        const float HeightDeltaX0{ Height10 - Height00 };
        const float HeightDeltaX1{ Height11 - Height01 };
        const float HeightDeltaZ0{ Height01 - Height00 };
        const float HeightDeltaZ1{ Height11 - Height10 };
        const float HeightDerivativeX{ ((1.0F - LocalGridZ) * HeightDeltaX0) + (LocalGridZ * HeightDeltaX1) };
        const float HeightDerivativeZ{ ((1.0F - LocalGridX) * HeightDeltaZ0) + (LocalGridX * HeightDeltaZ1) };
        const float SlopeX{ HeightDerivativeX / TerrainData.mHeightFieldCellSizeX };
        const float SlopeZ{ HeightDerivativeZ / TerrainData.mHeightFieldCellSizeZ };

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

    bool TryGetTerrainSurfaceAtWorldPosition(const Terrain::TerrainWorldData& TerrainData, float WorldX, float WorldZ, float& OutWorldHeight, DirectX::SimpleMath::Vector3& OutWorldNormal) {
        if (IsTerrainWorldDataValid(TerrainData) == false) {
            return false;
        }

        DirectX::SimpleMath::Matrix WorldMatrix{ BuildTerrainWorldMatrix(TerrainData) };
        DirectX::SimpleMath::Matrix InverseWorldMatrix{ WorldMatrix.Invert() };
        DirectX::SimpleMath::Matrix InverseTransposeWorldMatrix{ InverseWorldMatrix.Transpose() };
        DirectX::SimpleMath::Vector3 LocalPoint{ DirectX::SimpleMath::Vector3::Transform(DirectX::SimpleMath::Vector3{ WorldX, 0.0F, WorldZ }, InverseWorldMatrix) };

        float LocalHeight{};
        DirectX::SimpleMath::Vector3 LocalNormal{ DirectX::SimpleMath::Vector3::Up };
        const bool HasLocalSurface{ TryResolveTerrainSurfaceAtLocalPosition(TerrainData, LocalPoint.x, LocalPoint.z, LocalHeight, LocalNormal) };
        if (HasLocalSurface == false) {
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

    bool TryResolveTerrainRaySample(const Terrain::TerrainWorldData& TerrainData, const DirectX::SimpleMath::Ray& Ray, float RayDistance, float& OutTerrainDelta, DirectX::SimpleMath::Vector3& OutSurfacePosition, DirectX::SimpleMath::Vector3& OutSurfaceNormal) {
        const DirectX::SimpleMath::Vector3 RayPosition{ Ray.position + (Ray.direction * RayDistance) };
        if (MathUtility::IsFiniteVector3(RayPosition) == false) {
            return false;
        }

        float SurfaceHeight{};
        DirectX::SimpleMath::Vector3 SurfaceNormal{ DirectX::SimpleMath::Vector3::Up };
        if (TryGetTerrainSurfaceAtWorldPosition(TerrainData, RayPosition.x, RayPosition.z, SurfaceHeight, SurfaceNormal) == false || MathUtility::IsFiniteFloat(SurfaceHeight) == false || MathUtility::IsFiniteVector3(SurfaceNormal) == false) {
            return false;
        }

        const float TerrainDelta{ RayPosition.y - SurfaceHeight };
        if (MathUtility::IsFiniteFloat(TerrainDelta) == false) {
            return false;
        }

        OutTerrainDelta = TerrainDelta;
        OutSurfacePosition = DirectX::SimpleMath::Vector3{ RayPosition.x, SurfaceHeight, RayPosition.z };
        OutSurfaceNormal = SurfaceNormal;
        return true;
    }

    bool TryRaycastTerrainWorldData(const Terrain::TerrainWorldData& TerrainData, const DirectX::SimpleMath::Ray& Ray, float MaxDistance, DirectX::SimpleMath::Vector3& OutHitPosition, DirectX::SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) {
        if (MathUtility::IsFiniteVector3(Ray.position) == false || MathUtility::IsFiniteVector3(Ray.direction) == false || MathUtility::IsFiniteFloat(MaxDistance) == false) {
            return false;
        }

        DirectX::SimpleMath::Vector3 SafeRayDirection{ Ray.direction };
        const float RayDirectionLengthSquared{ SafeRayDirection.LengthSquared() };
        if (MathUtility::IsFiniteFloat(RayDirectionLengthSquared) == false || RayDirectionLengthSquared <= TerrainRaycastDistanceEpsilon) {
            return false;
        }

        SafeRayDirection.Normalize();
        if (MathUtility::IsFiniteVector3(SafeRayDirection) == false) {
            return false;
        }

        const float SafeMaxDistance{ std::max(MaxDistance, 0.0F) };
        if (SafeMaxDistance <= TerrainRaycastDistanceEpsilon) {
            return false;
        }

        const DirectX::SimpleMath::Ray SafeRay{ Ray.position, SafeRayDirection };
        const float WorldCellSizeX{ std::abs(TerrainData.mHeightFieldCellSizeX * TerrainData.mScale.x) };
        const float WorldCellSizeZ{ std::abs(TerrainData.mHeightFieldCellSizeZ * TerrainData.mScale.z) };
        const float MinimumCellSize{ std::min(WorldCellSizeX, WorldCellSizeZ) };
        const float SampleStepDistance{ std::max(MinimumCellSize * 0.5F, 0.05F) };
        if (MathUtility::IsFiniteFloat(SampleStepDistance) == false || SampleStepDistance <= 0.0F) {
            return false;
        }

        const std::uint32_t SampleStepCount{ std::max(static_cast<std::uint32_t>(1U), static_cast<std::uint32_t>(std::ceil(SafeMaxDistance / SampleStepDistance))) };
        bool IsPreviousSampleResolved{};
        float PreviousSampleDistance{};
        float PreviousSampleDelta{};
        DirectX::SimpleMath::Vector3 PreviousSurfacePosition{};
        DirectX::SimpleMath::Vector3 PreviousSurfaceNormal{ DirectX::SimpleMath::Vector3::Up };

        if (TryResolveTerrainRaySample(TerrainData, SafeRay, 0.0F, PreviousSampleDelta, PreviousSurfacePosition, PreviousSurfaceNormal) == true) {
            if (PreviousSampleDelta <= TerrainRaycastDeltaEpsilon) {
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
            const bool IsCurrentSampleResolved{ TryResolveTerrainRaySample(TerrainData, SafeRay, CurrentSampleDistance, CurrentSampleDelta, CurrentSurfacePosition, CurrentSurfaceNormal) };
            if (IsPreviousSampleResolved == true && IsCurrentSampleResolved == true) {
                const bool IsCrossedSurface{ (PreviousSampleDelta > 0.0F && CurrentSampleDelta <= 0.0F) || (PreviousSampleDelta < 0.0F && CurrentSampleDelta >= 0.0F) || std::abs(CurrentSampleDelta) <= TerrainRaycastDeltaEpsilon };
                if (IsCrossedSurface == true) {
                    float HitDistance{ CurrentSampleDistance };
                    const float DeltaDifference{ CurrentSampleDelta - PreviousSampleDelta };
                    if (std::abs(DeltaDifference) > TerrainRaycastDistanceEpsilon) {
                        const float HitAlpha{ std::clamp(PreviousSampleDelta / (PreviousSampleDelta - CurrentSampleDelta), 0.0F, 1.0F) };
                        HitDistance = PreviousSampleDistance + ((CurrentSampleDistance - PreviousSampleDistance) * HitAlpha);
                    }

                    float HitSampleDelta{};
                    DirectX::SimpleMath::Vector3 HitSurfacePosition{};
                    DirectX::SimpleMath::Vector3 HitSurfaceNormal{ DirectX::SimpleMath::Vector3::Up };
                    if (TryResolveTerrainRaySample(TerrainData, SafeRay, HitDistance, HitSampleDelta, HitSurfacePosition, HitSurfaceNormal) == false) {
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
      mTerrainWorldData{},
      mTerrainHandle{},
      mTerrainQuery{} {
}

PhysicsTerrainActor::~PhysicsTerrainActor() {
}

PhysicsTerrainActor::PhysicsTerrainActor(const PhysicsTerrainActor& Other)
    : PhysicsStaticActor{ Other },
      mTerrainWorldData{ Other.mTerrainWorldData },
      mTerrainHandle{ Other.mTerrainHandle },
      mTerrainQuery{ Other.mTerrainQuery } {
}

PhysicsTerrainActor& PhysicsTerrainActor::operator=(const PhysicsTerrainActor& Other) {
    if (this == &Other) {
        return *this;
    }

    PhysicsStaticActor::operator=(Other);
    mTerrainWorldData = Other.mTerrainWorldData;
    mTerrainHandle = Other.mTerrainHandle;
    mTerrainQuery = Other.mTerrainQuery;

    return *this;
}

PhysicsTerrainActor::PhysicsTerrainActor(PhysicsTerrainActor&& Other) noexcept
    : PhysicsStaticActor{ std::move(Other) },
      mTerrainWorldData{ std::move(Other.mTerrainWorldData) },
      mTerrainHandle{ Other.mTerrainHandle },
      mTerrainQuery{ Other.mTerrainQuery } {
    Other.mTerrainHandle = Terrain::TerrainDataHandle{};
    Other.mTerrainQuery = nullptr;
}

PhysicsTerrainActor& PhysicsTerrainActor::operator=(PhysicsTerrainActor&& Other) noexcept {
    if (this == &Other) {
        return *this;
    }

    PhysicsStaticActor::operator=(std::move(Other));
    mTerrainWorldData = std::move(Other.mTerrainWorldData);
    mTerrainHandle = Other.mTerrainHandle;
    mTerrainQuery = Other.mTerrainQuery;
    Other.mTerrainHandle = Terrain::TerrainDataHandle{};
    Other.mTerrainQuery = nullptr;

    return *this;
}

PhysicsTerrainActor::PhysicsTerrainActor(const ActorDesc& Desc)
    : PhysicsStaticActor{},
      mTerrainWorldData{},
      mTerrainHandle{},
      mTerrainQuery{} {
    SetActorDesc(Desc);
}

void PhysicsTerrainActor::SetActorDesc(const ActorDesc& Desc) {
    SetPosition(Desc.Position);
    SetRotation(Desc.Rotation);
    SetScale(Desc.Scale);
    mTerrainHandle = Desc.mTerrainHandle;
    mTerrainQuery = Desc.mTerrainQuery;

    if (Desc.mTerrainWorldData != nullptr) {
        mTerrainWorldData = Desc.mTerrainWorldData;
        if (IsPhysicsTerrainDataHandleValid(mTerrainHandle) == false) {
            mTerrainHandle = Desc.mTerrainWorldData->mHandle;
        }
        return;
    }

    const std::uint32_t TerrainId{ IsPhysicsTerrainDataHandleValid(mTerrainHandle) == true ? mTerrainHandle.mValue : 0U };
    if (Desc.HeightFieldValues != nullptr) {
        mTerrainWorldData = std::make_shared<const Terrain::TerrainWorldData>(BuildTerrainWorldDataFromActorDesc(Desc, TerrainId));
    }
    else {
        mTerrainWorldData.reset();
    }
}

PhysicsTerrainActor::ActorDesc PhysicsTerrainActor::GetActorDesc() const {
    ActorDesc Desc{};
    std::shared_ptr<const Terrain::TerrainWorldData> TerrainData{};
    if (TryResolveTerrainWorldData(TerrainData) == false) {
        return Desc;
    }

    Desc = BuildActorDescFromTerrainWorldData(*TerrainData);
    Desc.mTerrainHandle = mTerrainHandle;
    Desc.mTerrainQuery = mTerrainQuery;
    return Desc;
}

const std::shared_ptr<const Terrain::TerrainWorldData>& PhysicsTerrainActor::GetTerrainWorldData() const {
    return mTerrainWorldData;
}

Terrain::TerrainDataHandle PhysicsTerrainActor::GetTerrainHandle() const {
    return mTerrainHandle;
}

bool PhysicsTerrainActor::HasHeightFieldData() const {
    std::shared_ptr<const Terrain::TerrainWorldData> TerrainData{};
    if (TryResolveTerrainWorldData(TerrainData) == false) {
        return false;
    }

    return IsTerrainWorldDataValid(*TerrainData);
}

Terrain::TerrainWorldData PhysicsTerrainActor::BuildTerrainWorldDataFromActorDesc(const ActorDesc& Desc, std::uint32_t TerrainId) {
    Terrain::TerrainWorldData TerrainData{};
    if (Desc.mTerrainWorldData != nullptr) {
        TerrainData = *Desc.mTerrainWorldData;
    }

    TerrainData.mHandle = Desc.mTerrainHandle;
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
    if (Desc.HeightFieldValues != nullptr) {
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

    TerrainData.mSplatMapData = Desc.mSplatMapData;

    return TerrainData;
}

PhysicsTerrainActor::ActorDesc PhysicsTerrainActor::BuildActorDescFromTerrainWorldData(const Terrain::TerrainWorldData& TerrainData) {
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
    Desc.mSplatMapData = TerrainData.mSplatMapData;
    Desc.mTerrainWorldData = std::make_shared<const Terrain::TerrainWorldData>(TerrainData);
    Desc.mTerrainHandle = TerrainData.mHandle;
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
    Desc.HeightFieldValues = std::make_shared<const std::vector<float>>(HeightFieldValues);

    if (HeightFieldWidth > 1U) {
        Desc.HalfExtentX = static_cast<float>(HeightFieldWidth - 1U) * HeightFieldCellSizeX * 0.5F;
    }

    if (HeightFieldHeight > 1U) {
        Desc.HalfExtentZ = static_cast<float>(HeightFieldHeight - 1U) * HeightFieldCellSizeZ * 0.5F;
    }

    Desc.Scale = DirectX::SimpleMath::Vector3{ 1.0F, 1.0F, 1.0F };
    Desc.mTerrainWorldData = std::make_shared<const Terrain::TerrainWorldData>(BuildTerrainWorldDataFromActorDesc(Desc, 0U));
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
    if (mTerrainQuery != nullptr && IsPhysicsTerrainDataHandleValid(mTerrainHandle) == true) {
        return mTerrainQuery->TryGetSurfaceAtWorldPosition(mTerrainHandle, WorldX, WorldZ, OutWorldHeight, OutWorldNormal);
    }

    std::shared_ptr<const Terrain::TerrainWorldData> TerrainData{};
    if (TryResolveTerrainWorldData(TerrainData) == false) {
        return false;
    }

    bool HasSurface{ TryGetTerrainSurfaceAtWorldPosition(*TerrainData, WorldX, WorldZ, OutWorldHeight, OutWorldNormal) };
    return HasSurface;
}

bool PhysicsTerrainActor::TryRaycast(const DirectX::SimpleMath::Ray& Ray, float MaxDistance, DirectX::SimpleMath::Vector3& OutHitPosition, DirectX::SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) const {
    if (mTerrainQuery != nullptr && IsPhysicsTerrainDataHandleValid(mTerrainHandle) == true) {
        return mTerrainQuery->TryRaycast(mTerrainHandle, Ray, MaxDistance, OutHitPosition, OutHitNormal, OutHitDistance);
    }

    std::shared_ptr<const Terrain::TerrainWorldData> TerrainData{};
    if (TryResolveTerrainWorldData(TerrainData) == false) {
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

    std::shared_ptr<const Terrain::TerrainWorldData> TerrainData{};
    if (TryResolveTerrainWorldData(TerrainData) == false || IsTerrainWorldDataValid(*TerrainData) == false) {
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
        if (TryGetSurfaceAtWorldPosition(CornerWorldPosition.x, CornerWorldPosition.z, SurfaceWorldHeight, SurfaceWorldNormal) == false) {
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

bool PhysicsTerrainActor::TryResolveTerrainWorldData(std::shared_ptr<const Terrain::TerrainWorldData>& OutTerrainData) const {
    OutTerrainData.reset();
    if (mTerrainQuery != nullptr && IsPhysicsTerrainDataHandleValid(mTerrainHandle) == true) {
        if (mTerrainQuery->TryGetTerrainWorldData(mTerrainHandle, OutTerrainData) == true) {
            return true;
        }
    }

    if (mTerrainWorldData == nullptr) {
        return false;
    }

    OutTerrainData = mTerrainWorldData;
    return true;
}
