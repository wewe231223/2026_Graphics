#include "Game/Scene/TerrainHeightResolver.h"

#include <algorithm>
#include <cmath>
#include <utility>
#ifdef max
#undef max
#endif 

#ifdef min
#undef min
#endif 

namespace {
    constexpr float RaycastDistanceEpsilon{ 1.0e-4f };
    constexpr float RaycastDeltaEpsilon{ 1.0e-4f };

    bool IsFiniteFloat(const float Value) {
        return std::isfinite(Value) != 0;
    }

    bool IsFiniteVector3(const SimpleMath::Vector3& Value) {
        return IsFiniteFloat(Value.x) && IsFiniteFloat(Value.y) && IsFiniteFloat(Value.z);
    }

    bool TryResolveTerrainRaySample(const Game::TerrainHeightResolver& TerrainHeightResolverValue, const SimpleMath::Ray& Ray, const float RayDistance, float& OutTerrainDelta, SimpleMath::Vector3& OutSurfacePosition, SimpleMath::Vector3& OutSurfaceNormal) {
        const SimpleMath::Vector3 RayPosition{ Ray.position + (Ray.direction * RayDistance) };
        if (IsFiniteVector3(RayPosition) == false) {
            return false;
        }

        SimpleMath::Vector3 SurfacePosition{ RayPosition };
        SimpleMath::Vector3 SurfaceNormal{ SimpleMath::Vector3::Up };
        if (TerrainHeightResolverValue.TryResolvePositionYAndNormal(SurfacePosition, SurfaceNormal) == false || IsFiniteVector3(SurfacePosition) == false || IsFiniteVector3(SurfaceNormal) == false) {
            return false;
        }

        const float TerrainDelta{ RayPosition.y - SurfacePosition.y };
        if (IsFiniteFloat(TerrainDelta) == false) {
            return false;
        }

        OutTerrainDelta = TerrainDelta;
        OutSurfacePosition = SurfacePosition;
        OutSurfaceNormal = SurfaceNormal;
        return true;
    }
}

namespace Game {
    TerrainHeightResolver::TerrainHeightResolver()
        : mWidth{},
        mHeight{},
        mHeightValues{},
        mMaxHeight{ 1.0f },
        mCellSizeX{ 1.0f },
        mCellSizeZ{ 1.0f },
        mOriginOffsetX{},
        mOriginOffsetZ{},
        mCenterOrigin{},
        mInitialized{} {
    }

    TerrainHeightResolver::~TerrainHeightResolver() {
    }

    TerrainHeightResolver::TerrainHeightResolver(const TerrainHeightResolver& Other)
        : mWidth{ Other.mWidth },
        mHeight{ Other.mHeight },
        mHeightValues{ Other.mHeightValues },
        mMaxHeight{ Other.mMaxHeight },
        mCellSizeX{ Other.mCellSizeX },
        mCellSizeZ{ Other.mCellSizeZ },
        mOriginOffsetX{ Other.mOriginOffsetX },
        mOriginOffsetZ{ Other.mOriginOffsetZ },
        mCenterOrigin{ Other.mCenterOrigin },
        mInitialized{ Other.mInitialized } {
    }

    TerrainHeightResolver& TerrainHeightResolver::operator=(const TerrainHeightResolver& Other) {
        if (this == &Other) {
            return *this;
        }

        mWidth = Other.mWidth;
        mHeight = Other.mHeight;
        mHeightValues = Other.mHeightValues;
        mMaxHeight = Other.mMaxHeight;
        mCellSizeX = Other.mCellSizeX;
        mCellSizeZ = Other.mCellSizeZ;
        mOriginOffsetX = Other.mOriginOffsetX;
        mOriginOffsetZ = Other.mOriginOffsetZ;
        mCenterOrigin = Other.mCenterOrigin;
        mInitialized = Other.mInitialized;
        return *this;
    }

    TerrainHeightResolver::TerrainHeightResolver(TerrainHeightResolver&& Other) noexcept
        : mWidth{ Other.mWidth },
        mHeight{ Other.mHeight },
        mHeightValues{ std::move(Other.mHeightValues) },
        mMaxHeight{ Other.mMaxHeight },
        mCellSizeX{ Other.mCellSizeX },
        mCellSizeZ{ Other.mCellSizeZ },
        mOriginOffsetX{ Other.mOriginOffsetX },
        mOriginOffsetZ{ Other.mOriginOffsetZ },
        mCenterOrigin{ Other.mCenterOrigin },
        mInitialized{ Other.mInitialized } {
    }

    TerrainHeightResolver& TerrainHeightResolver::operator=(TerrainHeightResolver&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mWidth = Other.mWidth;
        mHeight = Other.mHeight;
        mHeightValues = std::move(Other.mHeightValues);
        mMaxHeight = Other.mMaxHeight;
        mCellSizeX = Other.mCellSizeX;
        mCellSizeZ = Other.mCellSizeZ;
        mOriginOffsetX = Other.mOriginOffsetX;
        mOriginOffsetZ = Other.mOriginOffsetZ;
        mCenterOrigin = Other.mCenterOrigin;
        mInitialized = Other.mInitialized;
        return *this;
    }

    void TerrainHeightResolver::Initialize(const HeightFieldData& HeightFieldDataValue, const TerrainBuildDesc& TerrainBuildDescValue) {
        mWidth = HeightFieldDataValue.Width;
        mHeight = HeightFieldDataValue.Height;
        mHeightValues = HeightFieldDataValue.HeightValues;
        mMaxHeight = TerrainBuildDescValue.MaxHeight;
        mCellSizeX = TerrainBuildDescValue.CellSizeX;
        mCellSizeZ = TerrainBuildDescValue.CellSizeZ;
        mCenterOrigin = TerrainBuildDescValue.CenterOrigin;
        mOriginOffsetX = (static_cast<float>(mWidth) - 1.0f) * mCellSizeX * 0.5f;
        mOriginOffsetZ = (static_cast<float>(mHeight) - 1.0f) * mCellSizeZ * 0.5f;
        mInitialized = true;
    }

    bool TerrainHeightResolver::TryResolvePositionY(SimpleMath::Vector3& InOutPosition) const {
        SimpleMath::Vector3 Normal{ SimpleMath::Vector3::Up };
        return TryResolvePositionYAndNormal(InOutPosition, Normal);
    }

    bool TerrainHeightResolver::TryResolvePositionYAndNormal(SimpleMath::Vector3& InOutPosition, SimpleMath::Vector3& OutNormal) const {
        if (mInitialized == false || mWidth < 2 || mHeight < 2 || mCellSizeX <= 0.0f || mCellSizeZ <= 0.0f) {
            return false;
        }

        float GridPositionX{ InOutPosition.x };
        float GridPositionZ{ InOutPosition.z };
        if (mCenterOrigin == true) {
            GridPositionX += mOriginOffsetX;
            GridPositionZ += mOriginOffsetZ;
        }

        const float MaxGridPositionX{ static_cast<float>(mWidth - 1) * mCellSizeX };
        const float MaxGridPositionZ{ static_cast<float>(mHeight - 1) * mCellSizeZ };
        if (GridPositionX < 0.0f || GridPositionZ < 0.0f || GridPositionX > MaxGridPositionX || GridPositionZ > MaxGridPositionZ) {
            return false;
        }

        const float GridX{ GridPositionX / mCellSizeX };
        const float GridZ{ GridPositionZ / mCellSizeZ };
        const std::uint32_t BaseGridX{ std::min(static_cast<std::uint32_t>(std::floor(GridX)), mWidth - 2) };
        const std::uint32_t BaseGridZ{ std::min(static_cast<std::uint32_t>(std::floor(GridZ)), mHeight - 2) };
        const std::uint32_t NextGridX{ BaseGridX + 1 };
        const std::uint32_t NextGridZ{ BaseGridZ + 1 };

        const float LocalX{ GridX - static_cast<float>(BaseGridX) };
        const float LocalZ{ GridZ - static_cast<float>(BaseGridZ) };

        const float Height00{ SampleCellHeight(BaseGridX, BaseGridZ) };
        const float Height10{ SampleCellHeight(NextGridX, BaseGridZ) };
        const float Height01{ SampleCellHeight(BaseGridX, NextGridZ) };
        const float Height11{ SampleCellHeight(NextGridX, NextGridZ) };
        const float HeightTop{ Height00 + ((Height10 - Height00) * LocalX) };
        const float HeightBottom{ Height01 + ((Height11 - Height01) * LocalX) };
        const float InterpolatedHeight{ HeightTop + ((HeightBottom - HeightTop) * LocalZ) };

        const float HeightDeltaX0{ Height10 - Height00 };
        const float HeightDeltaX1{ Height11 - Height01 };
        const float HeightDeltaZ0{ Height01 - Height00 };
        const float HeightDeltaZ1{ Height11 - Height10 };
        const float HeightDerivativeX{ ((1.0f - LocalZ) * HeightDeltaX0) + (LocalZ * HeightDeltaX1) };
        const float HeightDerivativeZ{ ((1.0f - LocalX) * HeightDeltaZ0) + (LocalX * HeightDeltaZ1) };
        const float SlopeX{ HeightDerivativeX / mCellSizeX };
        const float SlopeZ{ HeightDerivativeZ / mCellSizeZ };
        SimpleMath::Vector3 SurfaceNormal{ -SlopeX, 1.0f, -SlopeZ };
        const float SurfaceNormalLengthSquared{ SurfaceNormal.LengthSquared() };
        if (SurfaceNormalLengthSquared <= 0.0f) {
            SurfaceNormal = SimpleMath::Vector3::Up;
        }
        else {
            SurfaceNormal.Normalize();
        }

        InOutPosition.y = InterpolatedHeight;
        OutNormal = SurfaceNormal;
        return true;
    }

    bool TerrainHeightResolver::TryRaycast(const SimpleMath::Ray& Ray, const float MaxDistance, SimpleMath::Vector3& OutHitPosition, SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) const {
        if (mInitialized == false || mWidth < 2 || mHeight < 2 || mCellSizeX <= 0.0f || mCellSizeZ <= 0.0f || IsFiniteVector3(Ray.position) == false || IsFiniteVector3(Ray.direction) == false || IsFiniteFloat(MaxDistance) == false) {
            return false;
        }

        SimpleMath::Vector3 SafeRayDirection{ Ray.direction };
        const float RayDirectionLengthSquared{ SafeRayDirection.LengthSquared() };
        if (IsFiniteFloat(RayDirectionLengthSquared) == false || RayDirectionLengthSquared <= RaycastDistanceEpsilon) {
            return false;
        }

        SafeRayDirection.Normalize();
        if (IsFiniteVector3(SafeRayDirection) == false) {
            return false;
        }

        const float SafeMaxDistance{ (std::max)(MaxDistance, 0.0f) };
        if (SafeMaxDistance <= RaycastDistanceEpsilon) {
            return false;
        }

        const SimpleMath::Ray SafeRay{ Ray.position, SafeRayDirection };
        const float MinimumCellSize{ (std::min)(mCellSizeX, mCellSizeZ) };
        const float SampleStepDistance{ (std::max)(MinimumCellSize * 0.5f, 0.05f) };
        if (IsFiniteFloat(SampleStepDistance) == false || SampleStepDistance <= 0.0f) {
            return false;
        }

        const std::uint32_t SampleStepCount{ (std::max)(static_cast<std::uint32_t>(1), static_cast<std::uint32_t>(std::ceil(SafeMaxDistance / SampleStepDistance))) };
        bool IsPreviousSampleResolved{};
        float PreviousSampleDistance{};
        float PreviousSampleDelta{};
        SimpleMath::Vector3 PreviousSurfacePosition{};
        SimpleMath::Vector3 PreviousSurfaceNormal{ SimpleMath::Vector3::Up };

        if (TryResolveTerrainRaySample(*this, SafeRay, 0.0f, PreviousSampleDelta, PreviousSurfacePosition, PreviousSurfaceNormal) == true) {
            if (PreviousSampleDelta <= RaycastDeltaEpsilon) {
                OutHitPosition = PreviousSurfacePosition;
                OutHitNormal = PreviousSurfaceNormal;
                OutHitDistance = 0.0f;
                return true;
            }

            IsPreviousSampleResolved = true;
        }

        for (std::uint32_t SampleIndex{ 1 }; SampleIndex <= SampleStepCount; ++SampleIndex) {
            const float CurrentSampleDistance{ (std::min)(SafeMaxDistance, static_cast<float>(SampleIndex) * SampleStepDistance) };
            float CurrentSampleDelta{};
            SimpleMath::Vector3 CurrentSurfacePosition{};
            SimpleMath::Vector3 CurrentSurfaceNormal{ SimpleMath::Vector3::Up };
            const bool IsCurrentSampleResolved{ TryResolveTerrainRaySample(*this, SafeRay, CurrentSampleDistance, CurrentSampleDelta, CurrentSurfacePosition, CurrentSurfaceNormal) };

            if (IsPreviousSampleResolved == true && IsCurrentSampleResolved == true) {
                const bool IsCrossedSurface{ (PreviousSampleDelta > 0.0f && CurrentSampleDelta <= 0.0f) || (PreviousSampleDelta < 0.0f && CurrentSampleDelta >= 0.0f) || std::abs(CurrentSampleDelta) <= RaycastDeltaEpsilon };
                if (IsCrossedSurface == true) {
                    float HitDistance{ CurrentSampleDistance };
                    const float DeltaDifference{ CurrentSampleDelta - PreviousSampleDelta };
                    if (std::abs(DeltaDifference) > RaycastDistanceEpsilon) {
                        const float HitAlpha{ std::clamp(PreviousSampleDelta / (PreviousSampleDelta - CurrentSampleDelta), 0.0f, 1.0f) };
                        HitDistance = PreviousSampleDistance + ((CurrentSampleDistance - PreviousSampleDistance) * HitAlpha);
                    }

                    float HitSampleDelta{};
                    SimpleMath::Vector3 HitSurfacePosition{};
                    SimpleMath::Vector3 HitSurfaceNormal{ SimpleMath::Vector3::Up };
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

    std::uint32_t TerrainHeightResolver::GetWidth() const {
        return mWidth;
    }

    std::uint32_t TerrainHeightResolver::GetHeight() const {
        return mHeight;
    }

    const std::vector<float>& TerrainHeightResolver::GetHeightValues() const {
        return mHeightValues;
    }

    float TerrainHeightResolver::GetMaxHeight() const {
        return mMaxHeight;
    }

    float TerrainHeightResolver::GetCellSizeX() const {
        return mCellSizeX;
    }

    float TerrainHeightResolver::GetCellSizeZ() const {
        return mCellSizeZ;
    }

    bool TerrainHeightResolver::GetCenterOrigin() const {
        return mCenterOrigin;
    }

    bool TerrainHeightResolver::GetInitialized() const {
        return mInitialized;
    }

    std::uint32_t TerrainHeightResolver::CalculateHeightFieldIndex(std::uint32_t GridX, std::uint32_t GridZ) const {
        return (GridZ * mWidth) + GridX;
    }

    float TerrainHeightResolver::SampleCellHeight(std::uint32_t GridX, std::uint32_t GridZ) const {
        const std::uint32_t HeightFieldIndex{ CalculateHeightFieldIndex(GridX, GridZ) };
        const float Height01Value{ std::clamp(mHeightValues[HeightFieldIndex], 0.0f, 1.0f) };
        return Height01Value * mMaxHeight;
    }
}
