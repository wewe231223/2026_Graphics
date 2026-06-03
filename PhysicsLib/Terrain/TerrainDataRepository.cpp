#include "PhysicsLib/Terrain/TerrainDataRepository.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "Utility/MathValidation.h"

#undef min
#undef max

namespace {
constexpr float RaycastDistanceEpsilon{ 1.0E-4F };
constexpr float RaycastDeltaEpsilon{ 1.0E-4F };

DirectX::SimpleMath::Matrix BuildTerrainWorldMatrix(const TerrainWorldData& TerrainData) {
    DirectX::SimpleMath::Matrix ScalingMatrix{ DirectX::SimpleMath::Matrix::CreateScale(TerrainData.mScale) };
    DirectX::SimpleMath::Matrix RotationMatrix{ DirectX::SimpleMath::Matrix::CreateFromQuaternion(TerrainData.mOrientation) };
    DirectX::SimpleMath::Matrix TranslationMatrix{ DirectX::SimpleMath::Matrix::CreateTranslation(TerrainData.mPosition) };
    DirectX::SimpleMath::Matrix WorldMatrix{ ScalingMatrix * RotationMatrix * TranslationMatrix };
    return WorldMatrix;
}

std::size_t CalculateHeightFieldIndex(const TerrainWorldData& TerrainData, std::uint32_t X, std::uint32_t Z) {
    std::size_t Index{ static_cast<std::size_t>(Z) * static_cast<std::size_t>(TerrainData.mHeightFieldWidth) + static_cast<std::size_t>(X) };
    return Index;
}

float SampleCellHeight(const TerrainWorldData& TerrainData, std::uint32_t X, std::uint32_t Z) {
    const std::size_t HeightFieldIndex{ CalculateHeightFieldIndex(TerrainData, X, Z) };
    const float Height01Value{ std::clamp(TerrainData.mHeightFieldValues[HeightFieldIndex], 0.0F, 1.0F) };
    return Height01Value * TerrainData.mHeightFieldMaxHeight;
}

bool TryResolveTerrainSurfaceAtLocalPosition(const TerrainWorldData& TerrainData, float LocalX, float LocalZ, float& OutLocalHeight, DirectX::SimpleMath::Vector3& OutLocalNormal) {
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

bool TryResolveTerrainRaySample(const TerrainWorldData& TerrainData, const DirectX::SimpleMath::Ray& Ray, float RayDistance, float& OutTerrainDelta, DirectX::SimpleMath::Vector3& OutSurfacePosition, DirectX::SimpleMath::Vector3& OutSurfaceNormal) {
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
}

ITerrainQuery::ITerrainQuery() {
}

ITerrainQuery::~ITerrainQuery() {
}

ITerrainQuery::ITerrainQuery(const ITerrainQuery& Other) {
    (void)Other;
}

ITerrainQuery& ITerrainQuery::operator=(const ITerrainQuery& Other) {
    (void)Other;
    return *this;
}

ITerrainQuery::ITerrainQuery(ITerrainQuery&& Other) noexcept {
    (void)Other;
}

ITerrainQuery& ITerrainQuery::operator=(ITerrainQuery&& Other) noexcept {
    (void)Other;
    return *this;
}

TerrainDataRepository::TerrainDataRepository()
    : ITerrainQuery{},
      mSnapshotMutex{},
      mSnapshot{ std::make_shared<TerrainSnapshot>() } {
}

TerrainDataRepository::~TerrainDataRepository() {
}

void TerrainDataRepository::Clear() {
    std::lock_guard<std::mutex> SnapshotLock{ mSnapshotMutex };
    mSnapshot = std::make_shared<TerrainSnapshot>();
}

void TerrainDataRepository::PublishSnapshot(std::vector<TerrainWorldData> TerrainDataItems) {
    std::shared_ptr<TerrainSnapshot> NextSnapshot{ std::make_shared<TerrainSnapshot>() };
    NextSnapshot->mTerrainDataItems = std::move(TerrainDataItems);

    std::lock_guard<std::mutex> SnapshotLock{ mSnapshotMutex };
    mSnapshot = std::move(NextSnapshot);
}

void TerrainDataRepository::UpsertTerrainData(const TerrainWorldData& TerrainData) {
    std::lock_guard<std::mutex> SnapshotLock{ mSnapshotMutex };

    std::shared_ptr<TerrainSnapshot> NextSnapshot{ std::make_shared<TerrainSnapshot>() };
    if (mSnapshot != nullptr) {
        NextSnapshot->mTerrainDataItems = mSnapshot->mTerrainDataItems;
    }

    bool HasUpdated{};
    for (TerrainWorldData& CurrentTerrainData : NextSnapshot->mTerrainDataItems) {
        if (CurrentTerrainData.mTerrainId != TerrainData.mTerrainId) {
            continue;
        }

        CurrentTerrainData = TerrainData;
        HasUpdated = true;
        break;
    }

    if (HasUpdated == false) {
        NextSnapshot->mTerrainDataItems.push_back(TerrainData);
    }

    mSnapshot = std::move(NextSnapshot);
}

bool TerrainDataRepository::RemoveTerrainData(std::uint32_t TerrainId) {
    std::lock_guard<std::mutex> SnapshotLock{ mSnapshotMutex };
    if (mSnapshot == nullptr) {
        return false;
    }

    std::shared_ptr<TerrainSnapshot> NextSnapshot{ std::make_shared<TerrainSnapshot>() };
    NextSnapshot->mTerrainDataItems = mSnapshot->mTerrainDataItems;
    const auto NewEndIterator{ std::remove_if(NextSnapshot->mTerrainDataItems.begin(), NextSnapshot->mTerrainDataItems.end(), [TerrainId](const TerrainWorldData& TerrainData) {
        return TerrainData.mTerrainId == TerrainId;
    }) };
    const bool HasRemoved{ NewEndIterator != NextSnapshot->mTerrainDataItems.end() };
    NextSnapshot->mTerrainDataItems.erase(NewEndIterator, NextSnapshot->mTerrainDataItems.end());
    mSnapshot = std::move(NextSnapshot);
    return HasRemoved;
}

std::shared_ptr<const TerrainSnapshot> TerrainDataRepository::GetSnapshot() const {
    std::lock_guard<std::mutex> SnapshotLock{ mSnapshotMutex };
    return mSnapshot;
}

bool TerrainDataRepository::TryGetSurfaceAtWorldPosition(float WorldX, float WorldZ, float& OutWorldHeight, DirectX::SimpleMath::Vector3& OutWorldNormal) const {
    std::shared_ptr<const TerrainSnapshot> Snapshot{ GetSnapshot() };
    if (Snapshot == nullptr) {
        return false;
    }

    bool HasSurface{};
    float HighestSurfaceHeight{};
    DirectX::SimpleMath::Vector3 HighestSurfaceNormal{ DirectX::SimpleMath::Vector3::Up };
    for (const TerrainWorldData& TerrainData : Snapshot->mTerrainDataItems) {
        float SurfaceHeight{};
        DirectX::SimpleMath::Vector3 SurfaceNormal{ DirectX::SimpleMath::Vector3::Up };
        const bool HasCurrentSurface{ TryGetTerrainSurfaceAtWorldPosition(TerrainData, WorldX, WorldZ, SurfaceHeight, SurfaceNormal) };
        if (HasCurrentSurface == false) {
            continue;
        }

        if (HasSurface == false || SurfaceHeight > HighestSurfaceHeight) {
            HighestSurfaceHeight = SurfaceHeight;
            HighestSurfaceNormal = SurfaceNormal;
            HasSurface = true;
        }
    }

    if (HasSurface == false) {
        return false;
    }

    OutWorldHeight = HighestSurfaceHeight;
    OutWorldNormal = HighestSurfaceNormal;
    return true;
}

bool TerrainDataRepository::TryGetSurfaceHeightAtWorldPosition(float WorldX, float WorldZ, float& OutWorldHeight) const {
    DirectX::SimpleMath::Vector3 WorldNormal{ DirectX::SimpleMath::Vector3::Up };
    const bool HasSurface{ TryGetSurfaceAtWorldPosition(WorldX, WorldZ, OutWorldHeight, WorldNormal) };
    return HasSurface;
}

bool TerrainDataRepository::TryRaycast(const DirectX::SimpleMath::Ray& Ray, float MaxDistance, DirectX::SimpleMath::Vector3& OutHitPosition, DirectX::SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) const {
    std::shared_ptr<const TerrainSnapshot> Snapshot{ GetSnapshot() };
    if (Snapshot == nullptr) {
        return false;
    }

    bool HasHit{};
    float NearestHitDistance{ MaxDistance };
    DirectX::SimpleMath::Vector3 NearestHitPosition{};
    DirectX::SimpleMath::Vector3 NearestHitNormal{ DirectX::SimpleMath::Vector3::Up };
    for (const TerrainWorldData& TerrainData : Snapshot->mTerrainDataItems) {
        DirectX::SimpleMath::Vector3 HitPosition{};
        DirectX::SimpleMath::Vector3 HitNormal{ DirectX::SimpleMath::Vector3::Up };
        float HitDistance{};
        const bool HasCurrentHit{ TryRaycastTerrainWorldData(TerrainData, Ray, MaxDistance, HitPosition, HitNormal, HitDistance) };
        if (HasCurrentHit == false || HitDistance < 0.0F || HitDistance > MaxDistance) {
            continue;
        }

        if (HasHit == false || HitDistance < NearestHitDistance) {
            HasHit = true;
            NearestHitDistance = HitDistance;
            NearestHitPosition = HitPosition;
            NearestHitNormal = HitNormal;
        }
    }

    if (HasHit == false) {
        return false;
    }

    OutHitPosition = NearestHitPosition;
    OutHitNormal = NearestHitNormal;
    OutHitDistance = NearestHitDistance;
    return true;
}

bool IsTerrainWorldDataValid(const TerrainWorldData& TerrainData) {
    const std::size_t ExpectedHeightValueCount{ static_cast<std::size_t>(TerrainData.mHeightFieldWidth) * static_cast<std::size_t>(TerrainData.mHeightFieldHeight) };
    return TerrainData.mHeightFieldWidth > 1U && TerrainData.mHeightFieldHeight > 1U && TerrainData.mHeightFieldCellSizeX > 0.0F && TerrainData.mHeightFieldCellSizeZ > 0.0F && TerrainData.mHeightFieldMaxHeight > 0.0F && TerrainData.mHeightFieldValues.size() == ExpectedHeightValueCount;
}

bool AreTerrainWorldDataEquivalent(const TerrainWorldData& Left, const TerrainWorldData& Right) {
    return Left.mPosition == Right.mPosition && Left.mRotation == Right.mRotation && Left.mOrientation == Right.mOrientation && Left.mScale == Right.mScale && Left.mHalfExtentX == Right.mHalfExtentX && Left.mHalfExtentZ == Right.mHalfExtentZ && Left.mHeightFieldWidth == Right.mHeightFieldWidth && Left.mHeightFieldHeight == Right.mHeightFieldHeight && Left.mHeightFieldCellSizeX == Right.mHeightFieldCellSizeX && Left.mHeightFieldCellSizeZ == Right.mHeightFieldCellSizeZ && Left.mHeightFieldMaxHeight == Right.mHeightFieldMaxHeight && Left.mHeightFieldCenterOrigin == Right.mHeightFieldCenterOrigin && Left.mHeightFieldValues == Right.mHeightFieldValues;
}

bool TryGetTerrainSurfaceAtWorldPosition(const TerrainWorldData& TerrainData, float WorldX, float WorldZ, float& OutWorldHeight, DirectX::SimpleMath::Vector3& OutWorldNormal) {
    if (IsTerrainWorldDataValid(TerrainData) == false) {
        return false;
    }

    DirectX::SimpleMath::Matrix WorldMatrix{ BuildTerrainWorldMatrix(TerrainData) };
    DirectX::SimpleMath::Matrix InverseWorldMatrix{ WorldMatrix.Invert() };
    DirectX::SimpleMath::Matrix InverseTransposeWorldMatrix{ InverseWorldMatrix.Transpose() };
    DirectX::SimpleMath::Vector3 LocalPoint{ DirectX::SimpleMath::Vector3::Transform(DirectX::SimpleMath::Vector3{ WorldX, 0.0F, WorldZ }, InverseWorldMatrix) };

    float LocalHeight{};
    DirectX::SimpleMath::Vector3 LocalNormal{ DirectX::SimpleMath::Vector3::Up };
    bool HasLocalSurface{ TryResolveTerrainSurfaceAtLocalPosition(TerrainData, LocalPoint.x, LocalPoint.z, LocalHeight, LocalNormal) };
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

bool TryGetTerrainSurfaceHeightAtWorldPosition(const TerrainWorldData& TerrainData, float WorldX, float WorldZ, float& OutWorldHeight) {
    DirectX::SimpleMath::Vector3 WorldNormal{ DirectX::SimpleMath::Vector3::Up };
    const bool HasSurface{ TryGetTerrainSurfaceAtWorldPosition(TerrainData, WorldX, WorldZ, OutWorldHeight, WorldNormal) };
    return HasSurface;
}

bool TryRaycastTerrainWorldData(const TerrainWorldData& TerrainData, const DirectX::SimpleMath::Ray& Ray, float MaxDistance, DirectX::SimpleMath::Vector3& OutHitPosition, DirectX::SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) {
    if (MathUtility::IsFiniteVector3(Ray.position) == false || MathUtility::IsFiniteVector3(Ray.direction) == false || MathUtility::IsFiniteFloat(MaxDistance) == false) {
        return false;
    }

    DirectX::SimpleMath::Vector3 SafeRayDirection{ Ray.direction };
    const float RayDirectionLengthSquared{ SafeRayDirection.LengthSquared() };
    if (MathUtility::IsFiniteFloat(RayDirectionLengthSquared) == false || RayDirectionLengthSquared <= RaycastDistanceEpsilon) {
        return false;
    }

    SafeRayDirection.Normalize();
    if (MathUtility::IsFiniteVector3(SafeRayDirection) == false) {
        return false;
    }

    const float SafeMaxDistance{ std::max(MaxDistance, 0.0F) };
    if (SafeMaxDistance <= RaycastDistanceEpsilon) {
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
        const bool IsCurrentSampleResolved{ TryResolveTerrainRaySample(TerrainData, SafeRay, CurrentSampleDistance, CurrentSampleDelta, CurrentSurfacePosition, CurrentSurfaceNormal) };

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
