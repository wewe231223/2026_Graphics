#include "PhysicsWorld.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <limits>
#include <utility>

#include "PhysicsLib/Simulation/Repository/IPhysicsActorRepository.h"
#include "PhysicsLib/Simulation/Repository/PhysicsActorRepository.h"
#include "PhysicsLib/Simulation/SpatialQuery/BruteForcePhysicsSpatialQuery.h"
#include "PhysicsLib/Simulation/SpatialQuery/IPhysicsSpatialQuery.h"

#undef min
#undef max

#include "PhysicsLib/Actors/PhysicsDynamicCollisionInternal.inl"

namespace {
constexpr std::size_t DynamicCollisionSolverMinimumIterationCount{ 1U };
constexpr std::size_t DynamicCollisionSolverMaximumIterationCount{ 4U };
constexpr std::size_t DynamicCollisionPairsPerAdditionalIteration{ 24U };
constexpr std::uint32_t DynamicCcdMinimumSampleCount{ 4U };
constexpr std::uint32_t DynamicCcdMaximumSampleCount{ 32U };
constexpr float DynamicCcdMinimumExtent{ 0.05F };
constexpr float KinematicDynamicCcdPositionCorrectionSlop{ 0.002F };

struct AxisAlignedBounds {
    DirectX::SimpleMath::Vector3 mMinimum;
    DirectX::SimpleMath::Vector3 mMaximum;
};

struct DynamicActorSweepState {
    PhysicsDynamicActor* mActor;
    DirectX::SimpleMath::Vector3 mPreviousPosition;
    DirectX::SimpleMath::Quaternion mPreviousOrientation;
    DirectX::SimpleMath::Vector3 mPreviousScale;
    DirectX::SimpleMath::Vector3 mCurrentPosition;
    DirectX::SimpleMath::Quaternion mCurrentOrientation;
    DirectX::SimpleMath::Vector3 mCurrentScale;
    AxisAlignedBounds mPreviousBounds;
    AxisAlignedBounds mCurrentBounds;
    AxisAlignedBounds mSweptBounds;
};

struct DynamicSweepEntry {
    const DynamicActorSweepState* mSweepState;
};

struct KinematicDynamicSweepPairCandidate {
    const PhysicsKinematicActorSweepState* mKinematicSweepState;
    const DynamicActorSweepState* mDynamicSweepState;
};

DirectX::SimpleMath::Vector3 ResolveActorGravity(const IPhysicsWorldMediator& WorldMediator, const PhysicsActorBase& Actor) {
    DirectX::SimpleMath::Vector3 Gravity{};
    if (!Actor.HasFlag(PhysicsActorBase::PhysicsActorFlags::IgnoreGravity)) {
        Gravity = WorldMediator.GetGravity();
    }

    return Gravity;
}

DirectX::SimpleMath::Vector3 ResolveActorGravity(const DirectX::SimpleMath::Vector3& WorldGravity, const PhysicsActorBase& Actor) {
    DirectX::SimpleMath::Vector3 Gravity{};
    if (!Actor.HasFlag(PhysicsActorBase::PhysicsActorFlags::IgnoreGravity)) {
        Gravity = WorldGravity;
    }

    return Gravity;
}

DirectX::SimpleMath::Vector3 InterpolateVector3(const DirectX::SimpleMath::Vector3& StartValue, const DirectX::SimpleMath::Vector3& EndValue, float Alpha) {
    DirectX::SimpleMath::Vector3 InterpolatedValue{ StartValue + ((EndValue - StartValue) * Alpha) };
    return InterpolatedValue;
}

DirectX::SimpleMath::Quaternion NormalizeQuaternionOrIdentity(const DirectX::SimpleMath::Quaternion& QuaternionValue) {
    DirectX::SimpleMath::Quaternion NormalizedQuaternion{ QuaternionValue };
    if (NormalizedQuaternion.LengthSquared() <= 0.0F) {
        NormalizedQuaternion = DirectX::SimpleMath::Quaternion{ 0.0F, 0.0F, 0.0F, 1.0F };
    } else {
        NormalizedQuaternion.Normalize();
    }

    return NormalizedQuaternion;
}

DirectX::SimpleMath::Quaternion InterpolateQuaternion(const DirectX::SimpleMath::Quaternion& StartValue, const DirectX::SimpleMath::Quaternion& EndValue, float Alpha) {
    DirectX::SimpleMath::Quaternion StartOrientation{ NormalizeQuaternionOrIdentity(StartValue) };
    DirectX::SimpleMath::Quaternion EndOrientation{ NormalizeQuaternionOrIdentity(EndValue) };
    DirectX::SimpleMath::Quaternion InterpolatedOrientation{ DirectX::SimpleMath::Quaternion::Slerp(StartOrientation, EndOrientation, Alpha) };
    DirectX::SimpleMath::Quaternion NormalizedInterpolatedOrientation{ NormalizeQuaternionOrIdentity(InterpolatedOrientation) };
    return NormalizedInterpolatedOrientation;
}

AxisAlignedBounds MakeAxisAlignedBounds(const DirectX::BoundingOrientedBox& BoundingBox) {
    DirectX::XMFLOAT3 Corners[8]{};
    BoundingBox.GetCorners(Corners);

    AxisAlignedBounds Bounds{};
    Bounds.mMinimum = DirectX::SimpleMath::Vector3{ Corners[0].x, Corners[0].y, Corners[0].z };
    Bounds.mMaximum = Bounds.mMinimum;

    for (std::size_t CornerIndex{ 1U }; CornerIndex < 8U; ++CornerIndex) {
        Bounds.mMinimum.x = std::min(Bounds.mMinimum.x, Corners[CornerIndex].x);
        Bounds.mMinimum.y = std::min(Bounds.mMinimum.y, Corners[CornerIndex].y);
        Bounds.mMinimum.z = std::min(Bounds.mMinimum.z, Corners[CornerIndex].z);
        Bounds.mMaximum.x = std::max(Bounds.mMaximum.x, Corners[CornerIndex].x);
        Bounds.mMaximum.y = std::max(Bounds.mMaximum.y, Corners[CornerIndex].y);
        Bounds.mMaximum.z = std::max(Bounds.mMaximum.z, Corners[CornerIndex].z);
    }

    return Bounds;
}

AxisAlignedBounds MergeAxisAlignedBounds(const AxisAlignedBounds& FirstBounds, const AxisAlignedBounds& SecondBounds) {
    AxisAlignedBounds MergedBounds{};
    MergedBounds.mMinimum.x = std::min(FirstBounds.mMinimum.x, SecondBounds.mMinimum.x);
    MergedBounds.mMinimum.y = std::min(FirstBounds.mMinimum.y, SecondBounds.mMinimum.y);
    MergedBounds.mMinimum.z = std::min(FirstBounds.mMinimum.z, SecondBounds.mMinimum.z);
    MergedBounds.mMaximum.x = std::max(FirstBounds.mMaximum.x, SecondBounds.mMaximum.x);
    MergedBounds.mMaximum.y = std::max(FirstBounds.mMaximum.y, SecondBounds.mMaximum.y);
    MergedBounds.mMaximum.z = std::max(FirstBounds.mMaximum.z, SecondBounds.mMaximum.z);
    return MergedBounds;
}

bool IsOverlappingAxisAlignedBounds(const AxisAlignedBounds& FirstBounds, const AxisAlignedBounds& SecondBounds) {
    bool IsOverlappingX{ FirstBounds.mMinimum.x <= SecondBounds.mMaximum.x && SecondBounds.mMinimum.x <= FirstBounds.mMaximum.x };
    bool IsOverlappingY{ FirstBounds.mMinimum.y <= SecondBounds.mMaximum.y && SecondBounds.mMinimum.y <= FirstBounds.mMaximum.y };
    bool IsOverlappingZ{ FirstBounds.mMinimum.z <= SecondBounds.mMaximum.z && SecondBounds.mMinimum.z <= FirstBounds.mMaximum.z };
    bool IsOverlapping{ IsOverlappingX && IsOverlappingY && IsOverlappingZ };
    return IsOverlapping;
}

DirectX::BoundingOrientedBox CreateActorBoundingBoxAtTransform(const PhysicsActorBase& Actor, const DirectX::SimpleMath::Vector3& Position, const DirectX::SimpleMath::Quaternion& Orientation, const DirectX::SimpleMath::Vector3& Scale) {
    DirectX::SimpleMath::Matrix ScalingMatrix{ DirectX::SimpleMath::Matrix::CreateScale(Scale) };
    DirectX::SimpleMath::Matrix RotationMatrix{ DirectX::SimpleMath::Matrix::CreateFromQuaternion(Orientation) };
    DirectX::SimpleMath::Matrix TranslationMatrix{ DirectX::SimpleMath::Matrix::CreateTranslation(Position) };
    DirectX::SimpleMath::Matrix WorldMatrix{ ScalingMatrix * RotationMatrix * TranslationMatrix };
    DirectX::BoundingOrientedBox WorldBoundingBox{};
    Actor.GetLocalBoundingBox().Transform(WorldBoundingBox, WorldMatrix);
    return WorldBoundingBox;
}

float CalculateActorMinimumExtent(const PhysicsActorBase& Actor, const DirectX::SimpleMath::Vector3& Scale) {
    const DirectX::BoundingOrientedBox& LocalBoundingBox{ Actor.GetLocalBoundingBox() };
    float ExtentX{ std::abs(LocalBoundingBox.Extents.x * Scale.x) };
    float ExtentY{ std::abs(LocalBoundingBox.Extents.y * Scale.y) };
    float ExtentZ{ std::abs(LocalBoundingBox.Extents.z * Scale.z) };
    float MinimumExtent{ std::min(ExtentX, std::min(ExtentY, ExtentZ)) };
    MinimumExtent = std::max(MinimumExtent, DynamicCcdMinimumExtent);
    return MinimumExtent;
}

std::uint32_t CalculateSweepSampleCount(const DynamicActorSweepState& FirstState, const DynamicActorSweepState& SecondState) {
    float FirstMotionLength{ (FirstState.mCurrentPosition - FirstState.mPreviousPosition).Length() };
    float SecondMotionLength{ (SecondState.mCurrentPosition - SecondState.mPreviousPosition).Length() };
    float MaximumMotionLength{ std::max(FirstMotionLength, SecondMotionLength) };
    float FirstMinimumExtent{ CalculateActorMinimumExtent(*FirstState.mActor, FirstState.mPreviousScale) };
    float SecondMinimumExtent{ CalculateActorMinimumExtent(*SecondState.mActor, SecondState.mPreviousScale) };
    float MinimumExtent{ std::max(DynamicCcdMinimumExtent, std::min(FirstMinimumExtent, SecondMinimumExtent)) };
    std::uint32_t SampleCount{ static_cast<std::uint32_t>(std::ceil(MaximumMotionLength / MinimumExtent)) };
    SampleCount = std::clamp(SampleCount, DynamicCcdMinimumSampleCount, DynamicCcdMaximumSampleCount);
    return SampleCount;
}

DirectX::BoundingOrientedBox CreateSweepStateBoundingBoxAtAlpha(const DynamicActorSweepState& SweepState, float Alpha) {
    float ClampedAlpha{ std::clamp(Alpha, 0.0F, 1.0F) };
    DirectX::SimpleMath::Vector3 Position{ InterpolateVector3(SweepState.mPreviousPosition, SweepState.mCurrentPosition, ClampedAlpha) };
    DirectX::SimpleMath::Quaternion Orientation{ InterpolateQuaternion(SweepState.mPreviousOrientation, SweepState.mCurrentOrientation, ClampedAlpha) };
    DirectX::SimpleMath::Vector3 Scale{ InterpolateVector3(SweepState.mPreviousScale, SweepState.mCurrentScale, ClampedAlpha) };
    DirectX::BoundingOrientedBox BoundingBox{ CreateActorBoundingBoxAtTransform(*SweepState.mActor, Position, Orientation, Scale) };
    return BoundingBox;
}

bool IsSweepStatePairIntersectingAtAlpha(const DynamicActorSweepState& FirstState, const DynamicActorSweepState& SecondState, float Alpha) {
    DirectX::BoundingOrientedBox FirstBounds{ CreateSweepStateBoundingBoxAtAlpha(FirstState, Alpha) };
    DirectX::BoundingOrientedBox SecondBounds{ CreateSweepStateBoundingBoxAtAlpha(SecondState, Alpha) };
    bool IsIntersecting{ FirstBounds.Intersects(SecondBounds) };
    return IsIntersecting;
}

bool TryFindConservativeImpactAlpha(const DynamicActorSweepState& FirstState, const DynamicActorSweepState& SecondState, float& OutImpactAlpha) {
    if (IsSweepStatePairIntersectingAtAlpha(FirstState, SecondState, 0.0F)) {
        OutImpactAlpha = 0.0F;
        return true;
    }

    std::uint32_t SampleCount{ CalculateSweepSampleCount(FirstState, SecondState) };
    float PreviousAlpha{};
    for (std::uint32_t SampleIndex{ 1U }; SampleIndex <= SampleCount; ++SampleIndex) {
        float CurrentAlpha{ static_cast<float>(SampleIndex) / static_cast<float>(SampleCount) };
        if (!IsSweepStatePairIntersectingAtAlpha(FirstState, SecondState, CurrentAlpha)) {
            PreviousAlpha = CurrentAlpha;
            continue;
        }

        float LowAlpha{ PreviousAlpha };
        float HighAlpha{ CurrentAlpha };
        for (std::uint32_t IterationIndex{ 0U }; IterationIndex < 8U; ++IterationIndex) {
            float MidAlpha{ (LowAlpha + HighAlpha) * 0.5F };
            if (IsSweepStatePairIntersectingAtAlpha(FirstState, SecondState, MidAlpha)) {
                HighAlpha = MidAlpha;
            } else {
                LowAlpha = MidAlpha;
            }
        }

        OutImpactAlpha = HighAlpha;
        return true;
    }

    return false;
}

void SetActorTransformAtSweepAlpha(PhysicsDynamicActor& Actor, const DynamicActorSweepState& SweepState, float Alpha) {
    float ClampedAlpha{ std::clamp(Alpha, 0.0F, 1.0F) };
    DirectX::SimpleMath::Vector3 Position{ InterpolateVector3(SweepState.mPreviousPosition, SweepState.mCurrentPosition, ClampedAlpha) };
    DirectX::SimpleMath::Quaternion Orientation{ InterpolateQuaternion(SweepState.mPreviousOrientation, SweepState.mCurrentOrientation, ClampedAlpha) };
    DirectX::SimpleMath::Vector3 Scale{ InterpolateVector3(SweepState.mPreviousScale, SweepState.mCurrentScale, ClampedAlpha) };
    Actor.SetScale(Scale);
    Actor.SetOrientation(Orientation);
    Actor.SetPosition(Position);
}

std::vector<DynamicActorSweepState> CaptureDynamicActorSweepStates(const std::vector<PhysicsDynamicActor*>& DynamicActors) {
    std::vector<DynamicActorSweepState> SweepStates{};
    SweepStates.reserve(DynamicActors.size());
    std::size_t DynamicActorCount{ DynamicActors.size() };
    for (std::size_t ActorIndex{ 0U }; ActorIndex < DynamicActorCount; ++ActorIndex) {
        PhysicsDynamicActor* DynamicActor{ DynamicActors[ActorIndex] };
        if (DynamicActor == nullptr || !DynamicActor->GetIsActive() || DynamicActor->GetInverseMass() <= 0.0F) {
            continue;
        }

        DynamicActorSweepState SweepState{};
        SweepState.mActor = DynamicActor;
        SweepState.mPreviousPosition = DynamicActor->GetPosition();
        SweepState.mPreviousOrientation = DynamicActor->GetOrientation();
        SweepState.mPreviousScale = DynamicActor->GetScale();
        SweepState.mCurrentPosition = SweepState.mPreviousPosition;
        SweepState.mCurrentOrientation = SweepState.mPreviousOrientation;
        SweepState.mCurrentScale = SweepState.mPreviousScale;
        SweepState.mPreviousBounds = MakeAxisAlignedBounds(DynamicActor->GetWorldBoundingBox());
        SweepState.mCurrentBounds = SweepState.mPreviousBounds;
        SweepState.mSweptBounds = SweepState.mPreviousBounds;
        SweepStates.push_back(SweepState);
    }

    return SweepStates;
}

void CompleteDynamicActorSweepStates(std::vector<DynamicActorSweepState>& SweepStates) {
    std::size_t SweepStateCount{ SweepStates.size() };
    for (std::size_t SweepStateIndex{ 0U }; SweepStateIndex < SweepStateCount; ++SweepStateIndex) {
        DynamicActorSweepState& SweepState{ SweepStates[SweepStateIndex] };
        if (SweepState.mActor == nullptr) {
            continue;
        }

        SweepState.mCurrentPosition = SweepState.mActor->GetPosition();
        SweepState.mCurrentOrientation = SweepState.mActor->GetOrientation();
        SweepState.mCurrentScale = SweepState.mActor->GetScale();
        SweepState.mCurrentBounds = MakeAxisAlignedBounds(SweepState.mActor->GetWorldBoundingBox());
        SweepState.mSweptBounds = MergeAxisAlignedBounds(SweepState.mPreviousBounds, SweepState.mCurrentBounds);
    }
}

const DynamicActorSweepState* FindSweepStateForActor(const std::vector<DynamicActorSweepState>& SweepStates, const PhysicsDynamicActor* Actor) {
    std::size_t SweepStateCount{ SweepStates.size() };
    for (std::size_t SweepStateIndex{ 0U }; SweepStateIndex < SweepStateCount; ++SweepStateIndex) {
        const DynamicActorSweepState& SweepState{ SweepStates[SweepStateIndex] };
        if (SweepState.mActor == Actor) {
            return &SweepState;
        }
    }

    return nullptr;
}

std::vector<PhysicsDynamicCollisionPairCandidate> QuerySweptDynamicCollisionPairs(const std::vector<DynamicActorSweepState>& SweepStates) {
    std::vector<DynamicSweepEntry> SweepEntries{};
    SweepEntries.reserve(SweepStates.size());
    std::size_t SweepStateCount{ SweepStates.size() };
    for (std::size_t SweepStateIndex{ 0U }; SweepStateIndex < SweepStateCount; ++SweepStateIndex) {
        const DynamicActorSweepState& SweepState{ SweepStates[SweepStateIndex] };
        if (SweepState.mActor == nullptr) {
            continue;
        }

        SweepEntries.push_back(DynamicSweepEntry{ &SweepState });
    }

    std::vector<PhysicsDynamicCollisionPairCandidate> PairCandidates{};
    std::size_t SweepEntryCount{ SweepEntries.size() };
    if (SweepEntryCount < 2U) {
        return PairCandidates;
    }

    std::sort(SweepEntries.begin(), SweepEntries.end(), [](const DynamicSweepEntry& Left, const DynamicSweepEntry& Right) {
        return Left.mSweepState->mSweptBounds.mMinimum.x < Right.mSweepState->mSweptBounds.mMinimum.x;
    });

    PairCandidates.reserve(SweepEntryCount * 2U);
    for (std::size_t FirstIndex{ 0U }; FirstIndex < SweepEntryCount; ++FirstIndex) {
        const DynamicActorSweepState& FirstState{ *SweepEntries[FirstIndex].mSweepState };
        for (std::size_t SecondIndex{ FirstIndex + 1U }; SecondIndex < SweepEntryCount; ++SecondIndex) {
            const DynamicActorSweepState& SecondState{ *SweepEntries[SecondIndex].mSweepState };
            if (SecondState.mSweptBounds.mMinimum.x > FirstState.mSweptBounds.mMaximum.x) {
                break;
            }

            if (!IsOverlappingAxisAlignedBounds(FirstState.mSweptBounds, SecondState.mSweptBounds)) {
                continue;
            }

            DirectX::SimpleMath::Vector3 FirstMotionDelta{ FirstState.mCurrentPosition - FirstState.mPreviousPosition };
            DirectX::SimpleMath::Vector3 SecondMotionDelta{ SecondState.mCurrentPosition - SecondState.mPreviousPosition };
            if (FirstMotionDelta.LengthSquared() <= 0.0F && SecondMotionDelta.LengthSquared() <= 0.0F) {
                continue;
            }

            PairCandidates.push_back(PhysicsDynamicCollisionPairCandidate{ FirstState.mActor, SecondState.mActor });
        }
    }

    return PairCandidates;
}

DirectX::BoundingOrientedBox CreateKinematicSweepStateBoundingBoxAtAlpha(const PhysicsKinematicActorSweepState& SweepState, float Alpha) {
    float ClampedAlpha{ std::clamp(Alpha, 0.0F, 1.0F) };
    DirectX::SimpleMath::Vector3 Position{ InterpolateVector3(SweepState.mPreviousPosition, SweepState.mCurrentPosition, ClampedAlpha) };
    DirectX::SimpleMath::Quaternion Orientation{ InterpolateQuaternion(SweepState.mPreviousOrientation, SweepState.mCurrentOrientation, ClampedAlpha) };
    DirectX::SimpleMath::Vector3 Scale{ InterpolateVector3(SweepState.mPreviousScale, SweepState.mCurrentScale, ClampedAlpha) };
    DirectX::BoundingOrientedBox BoundingBox{ CreateActorBoundingBoxAtTransform(*SweepState.mActor, Position, Orientation, Scale) };
    return BoundingBox;
}

bool IsKinematicDynamicSweepStatePairIntersectingAtAlpha(const PhysicsKinematicActorSweepState& KinematicState, const DynamicActorSweepState& DynamicState, float Alpha) {
    DirectX::BoundingOrientedBox KinematicBounds{ CreateKinematicSweepStateBoundingBoxAtAlpha(KinematicState, Alpha) };
    DirectX::BoundingOrientedBox DynamicBounds{ CreateSweepStateBoundingBoxAtAlpha(DynamicState, Alpha) };
    bool IsIntersecting{ KinematicBounds.Intersects(DynamicBounds) };
    return IsIntersecting;
}

std::uint32_t CalculateKinematicDynamicSweepSampleCount(const PhysicsKinematicActorSweepState& KinematicState, const DynamicActorSweepState& DynamicState) {
    float KinematicMotionLength{ (KinematicState.mCurrentPosition - KinematicState.mPreviousPosition).Length() };
    float DynamicMotionLength{ (DynamicState.mCurrentPosition - DynamicState.mPreviousPosition).Length() };
    float MaximumMotionLength{ std::max(KinematicMotionLength, DynamicMotionLength) };
    float KinematicMinimumExtent{ CalculateActorMinimumExtent(*KinematicState.mActor, KinematicState.mPreviousScale) };
    float DynamicMinimumExtent{ CalculateActorMinimumExtent(*DynamicState.mActor, DynamicState.mPreviousScale) };
    float MinimumExtent{ std::max(DynamicCcdMinimumExtent, std::min(KinematicMinimumExtent, DynamicMinimumExtent)) };
    std::uint32_t SampleCount{ static_cast<std::uint32_t>(std::ceil(MaximumMotionLength / MinimumExtent)) };
    SampleCount = std::clamp(SampleCount, DynamicCcdMinimumSampleCount, DynamicCcdMaximumSampleCount);
    return SampleCount;
}

bool TryFindConservativeKinematicDynamicImpactAlpha(const PhysicsKinematicActorSweepState& KinematicState, const DynamicActorSweepState& DynamicState, float& OutImpactAlpha) {
    if (IsKinematicDynamicSweepStatePairIntersectingAtAlpha(KinematicState, DynamicState, 0.0F)) {
        OutImpactAlpha = 0.0F;
        return true;
    }

    std::uint32_t SampleCount{ CalculateKinematicDynamicSweepSampleCount(KinematicState, DynamicState) };
    float PreviousAlpha{};
    for (std::uint32_t SampleIndex{ 1U }; SampleIndex <= SampleCount; ++SampleIndex) {
        float CurrentAlpha{ static_cast<float>(SampleIndex) / static_cast<float>(SampleCount) };
        if (!IsKinematicDynamicSweepStatePairIntersectingAtAlpha(KinematicState, DynamicState, CurrentAlpha)) {
            PreviousAlpha = CurrentAlpha;
            continue;
        }

        float LowAlpha{ PreviousAlpha };
        float HighAlpha{ CurrentAlpha };
        for (std::uint32_t IterationIndex{ 0U }; IterationIndex < 8U; ++IterationIndex) {
            float MidAlpha{ (LowAlpha + HighAlpha) * 0.5F };
            if (IsKinematicDynamicSweepStatePairIntersectingAtAlpha(KinematicState, DynamicState, MidAlpha)) {
                HighAlpha = MidAlpha;
            } else {
                LowAlpha = MidAlpha;
            }
        }

        OutImpactAlpha = HighAlpha;
        return true;
    }

    return false;
}

AxisAlignedBounds ConvertKinematicSweepBounds(const PhysicsKinematicActorSweepBounds& Bounds) {
    AxisAlignedBounds ConvertedBounds{ Bounds.mMinimum, Bounds.mMaximum };
    return ConvertedBounds;
}

std::vector<KinematicDynamicSweepPairCandidate> QuerySweptKinematicDynamicCollisionPairs(const std::vector<PhysicsKinematicActorSweepState>& KinematicSweepStates, const std::vector<DynamicActorSweepState>& DynamicSweepStates) {
    std::vector<KinematicDynamicSweepPairCandidate> PairCandidates{};
    if (KinematicSweepStates.empty() || DynamicSweepStates.empty()) {
        return PairCandidates;
    }

    PairCandidates.reserve(KinematicSweepStates.size() * DynamicSweepStates.size());
    std::size_t KinematicSweepStateCount{ KinematicSweepStates.size() };
    std::size_t DynamicSweepStateCount{ DynamicSweepStates.size() };
    for (std::size_t KinematicStateIndex{ 0U }; KinematicStateIndex < KinematicSweepStateCount; ++KinematicStateIndex) {
        const PhysicsKinematicActorSweepState& KinematicState{ KinematicSweepStates[KinematicStateIndex] };
        if (KinematicState.mActor == nullptr || !KinematicState.mActor->GetIsActive() || KinematicState.mIsTeleport || !KinematicState.mCanUseForCcd) {
            continue;
        }

        AxisAlignedBounds KinematicSweptBounds{ ConvertKinematicSweepBounds(KinematicState.mSweptBounds) };
        for (std::size_t DynamicStateIndex{ 0U }; DynamicStateIndex < DynamicSweepStateCount; ++DynamicStateIndex) {
            const DynamicActorSweepState& DynamicState{ DynamicSweepStates[DynamicStateIndex] };
            if (DynamicState.mActor == nullptr || !DynamicState.mActor->GetIsActive() || DynamicState.mActor->GetInverseMass() <= 0.0F) {
                continue;
            }

            DirectX::SimpleMath::Vector3 KinematicMotionDelta{ KinematicState.mCurrentPosition - KinematicState.mPreviousPosition };
            DirectX::SimpleMath::Vector3 DynamicMotionDelta{ DynamicState.mCurrentPosition - DynamicState.mPreviousPosition };
            if (KinematicMotionDelta.LengthSquared() <= 0.0F && DynamicMotionDelta.LengthSquared() <= 0.0F) {
                continue;
            }

            if (!IsOverlappingAxisAlignedBounds(KinematicSweptBounds, DynamicState.mSweptBounds)) {
                continue;
            }

            PairCandidates.push_back(KinematicDynamicSweepPairCandidate{ &KinematicState, &DynamicState });
        }
    }

    return PairCandidates;
}

PhysicsFrameAccumulator::ActorState CreateActorStateFromActor(const PhysicsActorBase& Actor) {
    PhysicsFrameAccumulator::ActorState ActorStateValue{ &Actor, Actor.GetActorType(), Actor.GetPosition(), Actor.GetOrientation(), Actor.GetScale() };
    return ActorStateValue;
}

void IntegrateDynamicActors(IPhysicsWorldMediator& WorldMediator, IPhysicsActorRepository& ActorRepository, std::vector<PhysicsDynamicActor*>& DynamicActors, float DeltaTime) {
    ActorRepository.CollectDynamicActors(DynamicActors);
    std::size_t DynamicActorCount{ DynamicActors.size() };
    for (std::size_t ActorIndex{ 0U }; ActorIndex < DynamicActorCount; ++ActorIndex) {
        PhysicsDynamicActor* DynamicActor{ DynamicActors[ActorIndex] };
        if (DynamicActor == nullptr) {
            continue;
        }

        DynamicActor->Integrate(WorldMediator, DeltaTime);
        DynamicActor->SolveConstraints(WorldMediator, DeltaTime);
    }
}

void ResolveKinematicCollisions(IPhysicsActorRepository& ActorRepository, const DirectX::SimpleMath::Vector3& Gravity, float DeltaTime) {
    if (DeltaTime <= 0.0F) {
        return;
    }

    std::size_t ActorCount{ ActorRepository.GetActorCount() };
    if (ActorCount < 2U) {
        return;
    }

    for (std::size_t FirstActorIndex{ 0U }; FirstActorIndex < ActorCount; ++FirstActorIndex) {
        PhysicsActorBase* FirstActor{ ActorRepository.GetActor(FirstActorIndex) };
        if (FirstActor == nullptr) {
            continue;
        }

        for (std::size_t SecondActorIndex{ FirstActorIndex + 1U }; SecondActorIndex < ActorCount; ++SecondActorIndex) {
            PhysicsActorBase* SecondActor{ ActorRepository.GetActor(SecondActorIndex) };
            if (SecondActor == nullptr) {
                continue;
            }

            if (FirstActor->GetActorType() == PhysicsActorBase::PhysicsActorType::Kinematic) {
                if (SecondActor->GetActorType() == PhysicsActorBase::PhysicsActorType::Static) {
                    continue;
                }

                bool HasCollision{ FirstActor->ResolveActorCollision(*SecondActor, DeltaTime) };
                if (HasCollision && SecondActor->GetActorType() == PhysicsActorBase::PhysicsActorType::Dynamic) {
                    SecondActor->UpdateSleepState(ResolveActorGravity(Gravity, *SecondActor));
                }

                continue;
            }

            if (SecondActor->GetActorType() == PhysicsActorBase::PhysicsActorType::Kinematic) {
                if (FirstActor->GetActorType() == PhysicsActorBase::PhysicsActorType::Static) {
                    continue;
                }

                bool HasCollision{ SecondActor->ResolveActorCollision(*FirstActor, DeltaTime) };
                if (HasCollision && FirstActor->GetActorType() == PhysicsActorBase::PhysicsActorType::Dynamic) {
                    FirstActor->UpdateSleepState(ResolveActorGravity(Gravity, *FirstActor));
                }
            }
        }
    }
}

bool ResolveDynamicCollisionPair(IPhysicsWorldMediator& WorldMediator, PhysicsDynamicActor& FirstActor, PhysicsDynamicActor& SecondActor, float DeltaTime) {
    bool HasCollision{ FirstActor.ResolveActorCollision(SecondActor, DeltaTime) };
    if (HasCollision) {
        WorldMediator.PublishEvent(PhysicsSimulationEventType::DynamicCollisionResolved, &FirstActor, &SecondActor);
    }

    return HasCollision;
}

std::uintptr_t GetActorPointerValue(const PhysicsDynamicActor* Actor) {
    std::uintptr_t PointerValue{ reinterpret_cast<std::uintptr_t>(Actor) };
    return PointerValue;
}

void SortAndDeduplicatePairCandidates(std::vector<PhysicsDynamicCollisionPairCandidate>& PairCandidates) {
    std::sort(PairCandidates.begin(), PairCandidates.end(), [](const PhysicsDynamicCollisionPairCandidate& LeftPair, const PhysicsDynamicCollisionPairCandidate& RightPair) {
        std::uintptr_t LeftFirstPointer{ GetActorPointerValue(LeftPair.mFirstActor) };
        std::uintptr_t LeftSecondPointer{ GetActorPointerValue(LeftPair.mSecondActor) };
        if (LeftFirstPointer > LeftSecondPointer) {
            std::swap(LeftFirstPointer, LeftSecondPointer);
        }

        std::uintptr_t RightFirstPointer{ GetActorPointerValue(RightPair.mFirstActor) };
        std::uintptr_t RightSecondPointer{ GetActorPointerValue(RightPair.mSecondActor) };
        if (RightFirstPointer > RightSecondPointer) {
            std::swap(RightFirstPointer, RightSecondPointer);
        }

        if (LeftFirstPointer == RightFirstPointer) {
            return LeftSecondPointer < RightSecondPointer;
        }

        return LeftFirstPointer < RightFirstPointer;
    });

    std::size_t WriteIndex{};
    for (std::size_t ReadIndex{ 0U }; ReadIndex < PairCandidates.size(); ++ReadIndex) {
        PhysicsDynamicActor* FirstActor{ PairCandidates[ReadIndex].mFirstActor };
        PhysicsDynamicActor* SecondActor{ PairCandidates[ReadIndex].mSecondActor };
        if (FirstActor == nullptr || SecondActor == nullptr || FirstActor == SecondActor) {
            continue;
        }

        std::uintptr_t FirstPointer{ GetActorPointerValue(FirstActor) };
        std::uintptr_t SecondPointer{ GetActorPointerValue(SecondActor) };
        if (FirstPointer > SecondPointer) {
            std::swap(FirstPointer, SecondPointer);
            std::swap(FirstActor, SecondActor);
        }

        if (WriteIndex > 0U) {
            std::uintptr_t PreviousFirstPointer{ GetActorPointerValue(PairCandidates[WriteIndex - 1U].mFirstActor) };
            std::uintptr_t PreviousSecondPointer{ GetActorPointerValue(PairCandidates[WriteIndex - 1U].mSecondActor) };
            if (PreviousFirstPointer == FirstPointer && PreviousSecondPointer == SecondPointer) {
                continue;
            }
        }

        PairCandidates[WriteIndex] = PhysicsDynamicCollisionPairCandidate{ FirstActor, SecondActor };
        ++WriteIndex;
    }

    PairCandidates.resize(WriteIndex);
}

void ResolveDynamicCollisions(IPhysicsWorldMediator& WorldMediator, std::vector<PhysicsDynamicCollisionPairCandidate>& PairCandidates, float DeltaTime) {
    PhysicsDynamicCollisionSolver::BeginFrame(PairCandidates.size());
    SortAndDeduplicatePairCandidates(PairCandidates);
    std::size_t PairCandidateCount{ PairCandidates.size() };
    if (PairCandidateCount == 0U) {
        PhysicsDynamicCollisionSolver::EndFrame();
        return;
    }

    std::size_t AdditionalIterationCount{ PairCandidateCount / DynamicCollisionPairsPerAdditionalIteration };
    std::size_t SolverIterationCount{ DynamicCollisionSolverMinimumIterationCount + AdditionalIterationCount };
    if (SolverIterationCount > DynamicCollisionSolverMaximumIterationCount) {
        SolverIterationCount = DynamicCollisionSolverMaximumIterationCount;
    }

    for (std::size_t IterationIndex{ 0U }; IterationIndex < SolverIterationCount; ++IterationIndex) {
        bool HasAnyCollision{};
        for (std::size_t PairIndex{ 0U }; PairIndex < PairCandidateCount; ++PairIndex) {
            PhysicsDynamicActor* FirstActor{ PairCandidates[PairIndex].mFirstActor };
            PhysicsDynamicActor* SecondActor{ PairCandidates[PairIndex].mSecondActor };
            if (FirstActor == nullptr || SecondActor == nullptr) {
                continue;
            }

            if (!FirstActor->GetIsActive() || FirstActor->GetInverseMass() <= 0.0F) {
                continue;
            }

            if (!SecondActor->GetIsActive() || SecondActor->GetInverseMass() <= 0.0F) {
                continue;
            }

            bool HasCollision{ ResolveDynamicCollisionPair(WorldMediator, *FirstActor, *SecondActor, DeltaTime) };
            if (!HasCollision) {
                continue;
            }

            HasAnyCollision = true;
            FirstActor->UpdateSleepState(ResolveActorGravity(WorldMediator, *FirstActor));
            SecondActor->UpdateSleepState(ResolveActorGravity(WorldMediator, *SecondActor));
        }

        if (!HasAnyCollision) {
            break;
        }
    }

    PhysicsDynamicCollisionSolver::EndFrame();
}

bool TryResolveSweptDynamicCollisionPair(IPhysicsWorldMediator& WorldMediator, const DynamicActorSweepState& FirstState, const DynamicActorSweepState& SecondState, float DeltaTime) {
    PhysicsDynamicActor* FirstActor{ FirstState.mActor };
    PhysicsDynamicActor* SecondActor{ SecondState.mActor };
    if (FirstActor == nullptr || SecondActor == nullptr || FirstActor == SecondActor) {
        return false;
    }

    if (!FirstActor->GetIsActive() || FirstActor->GetInverseMass() <= 0.0F) {
        return false;
    }

    if (!SecondActor->GetIsActive() || SecondActor->GetInverseMass() <= 0.0F) {
        return false;
    }

    if (FirstActor->GetWorldBoundingBox().Intersects(SecondActor->GetWorldBoundingBox())) {
        return false;
    }

    if (IsSweepStatePairIntersectingAtAlpha(FirstState, SecondState, 0.0F)) {
        return false;
    }

    float ImpactAlpha{};
    bool HasImpactAlpha{ TryFindConservativeImpactAlpha(FirstState, SecondState, ImpactAlpha) };
    if (!HasImpactAlpha) {
        return false;
    }

    SetActorTransformAtSweepAlpha(*FirstActor, FirstState, ImpactAlpha);
    SetActorTransformAtSweepAlpha(*SecondActor, SecondState, ImpactAlpha);

    bool HasCollision{ ResolveDynamicCollisionPair(WorldMediator, *FirstActor, *SecondActor, DeltaTime) };
    if (!HasCollision) {
        SetActorTransformAtSweepAlpha(*FirstActor, FirstState, 1.0F);
        SetActorTransformAtSweepAlpha(*SecondActor, SecondState, 1.0F);
        return false;
    }

    FirstActor->UpdateSleepState(ResolveActorGravity(WorldMediator, *FirstActor));
    SecondActor->UpdateSleepState(ResolveActorGravity(WorldMediator, *SecondActor));
    return true;
}

void ResolveSweptDynamicCollisions(IPhysicsWorldMediator& WorldMediator, const std::vector<DynamicActorSweepState>& SweepStates, std::vector<PhysicsDynamicCollisionPairCandidate>& PairCandidates, float DeltaTime) {
    SortAndDeduplicatePairCandidates(PairCandidates);
    std::size_t PairCandidateCount{ PairCandidates.size() };
    for (std::size_t PairIndex{ 0U }; PairIndex < PairCandidateCount; ++PairIndex) {
        PhysicsDynamicActor* FirstActor{ PairCandidates[PairIndex].mFirstActor };
        PhysicsDynamicActor* SecondActor{ PairCandidates[PairIndex].mSecondActor };
        if (FirstActor == nullptr || SecondActor == nullptr) {
            continue;
        }

        const DynamicActorSweepState* FirstState{ FindSweepStateForActor(SweepStates, FirstActor) };
        const DynamicActorSweepState* SecondState{ FindSweepStateForActor(SweepStates, SecondActor) };
        if (FirstState == nullptr || SecondState == nullptr) {
            continue;
        }

        TryResolveSweptDynamicCollisionPair(WorldMediator, *FirstState, *SecondState, DeltaTime);
    }
}

bool ApplyKinematicDynamicCcdImpulse(const PhysicsKinematicActorSweepState& KinematicState, PhysicsDynamicActor& DynamicActor, const DynamicContactManifold& ContactManifold, const DirectX::SimpleMath::Vector3& CollisionNormal, float ImpulseMagnitudeClamp) {
    if (ContactManifold.mContactPointCount == 0U) {
        return false;
    }

    float EffectiveRestitution{ std::min(KinematicState.mActor->GetRestitution(), DynamicActor.GetRestitution()) };
    float SafeContactPointCount{ static_cast<float>(std::max<std::size_t>(ContactManifold.mContactPointCount, 1U)) };
    float SafeImpulseMagnitudeClamp{ ImpulseMagnitudeClamp > 0.0F ? ImpulseMagnitudeClamp : std::numeric_limits<float>::max() };
    float ContactImpulseClamp{ SafeImpulseMagnitudeClamp / SafeContactPointCount };
    bool HasAppliedImpulse{};
    for (std::size_t ContactPointIndex{ 0U }; ContactPointIndex < ContactManifold.mContactPointCount; ++ContactPointIndex) {
        const DynamicContactPoint& ContactPoint{ ContactManifold.mContactPoints[ContactPointIndex] };
        DirectX::SimpleMath::Vector3 DynamicContactVelocity{ CalculateVelocityAtPoint(DynamicActor, ContactPoint.mPosition) };
        DirectX::SimpleMath::Vector3 RelativeVelocity{ DynamicContactVelocity - KinematicState.mVelocity };
        float VelocityAlongNormal{ RelativeVelocity.Dot(CollisionNormal) };
        if (VelocityAlongNormal >= 0.0F) {
            continue;
        }

        DirectX::SimpleMath::Vector3 ContactOffset{ ContactPoint.mPosition - DynamicActor.GetPosition() };
        float NormalDenominator{ CalculateSingleActorContactImpulseDenominator(DynamicActor, ContactOffset, CollisionNormal) };
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
        DynamicActor.ApplyImpulseAtPoint(NormalImpulse, ContactPoint.mPosition);
        HasAppliedImpulse = true;
    }

    return HasAppliedImpulse;
}

bool ResolveKinematicDynamicCcdAtImpact(const PhysicsKinematicActorSweepState& KinematicState, PhysicsDynamicActor& DynamicActor, const DirectX::BoundingOrientedBox& KinematicBounds, const DirectX::BoundingOrientedBox& DynamicBounds, const DynamicSatResult& SatResult, float ImpulseMagnitudeClamp) {
    if (!SatResult.mIntersect) {
        return false;
    }

    DirectX::SimpleMath::Vector3 CollisionNormal{ NormalizeOrZero(SatResult.mNormal) };
    if (IsNearlyZeroVector(CollisionNormal, DynamicSatAxisEpsilon)) {
        return false;
    }

    DynamicObb KinematicObb{ CreateDynamicObb(KinematicBounds) };
    DynamicObb DynamicObbValue{ CreateDynamicObb(DynamicBounds) };
    DynamicContactManifold ContactManifold{ BuildContactManifold(KinematicBounds, DynamicBounds, KinematicObb, DynamicObbValue, SatResult) };
    bool HasAppliedImpulse{ ApplyKinematicDynamicCcdImpulse(KinematicState, DynamicActor, ContactManifold, CollisionNormal, ImpulseMagnitudeClamp) };

    float CorrectedPenetration{ std::max(0.0F, SatResult.mPenetration - KinematicDynamicCcdPositionCorrectionSlop) };
    if (CorrectedPenetration > 0.0F) {
        DirectX::SimpleMath::Vector3 CorrectedPosition{ DynamicActor.GetPosition() + (CollisionNormal * CorrectedPenetration) };
        DynamicActor.SetPosition(CorrectedPosition);
    }

    if (!HasAppliedImpulse && CorrectedPenetration <= 0.0F) {
        return false;
    }

    DynamicActor.RegisterContactNormal(CollisionNormal);
    DynamicActor.SetIsSleeping(false);
    return true;
}

bool TryResolveSweptKinematicDynamicCollisionPair(IPhysicsWorldMediator& WorldMediator, const PhysicsKinematicActorSweepState& KinematicState, const DynamicActorSweepState& DynamicState, float DeltaTime, float ImpulseMagnitudeClamp) {
    (void)DeltaTime;

    PhysicsKinematicActor* KinematicActor{ KinematicState.mActor };
    PhysicsDynamicActor* DynamicActor{ DynamicState.mActor };
    if (KinematicActor == nullptr || DynamicActor == nullptr) {
        return false;
    }

    if (!KinematicActor->GetIsActive() || !DynamicActor->GetIsActive() || DynamicActor->GetInverseMass() <= 0.0F) {
        return false;
    }

    if (KinematicState.mIsTeleport || !KinematicState.mCanUseForCcd) {
        return false;
    }

    if (KinematicActor->GetWorldBoundingBox().Intersects(DynamicActor->GetWorldBoundingBox())) {
        return false;
    }

    if (IsKinematicDynamicSweepStatePairIntersectingAtAlpha(KinematicState, DynamicState, 0.0F)) {
        return false;
    }

    float ImpactAlpha{};
    bool HasImpactAlpha{ TryFindConservativeKinematicDynamicImpactAlpha(KinematicState, DynamicState, ImpactAlpha) };
    if (!HasImpactAlpha) {
        return false;
    }

    SetActorTransformAtSweepAlpha(*DynamicActor, DynamicState, ImpactAlpha);
    DirectX::BoundingOrientedBox KinematicBounds{ CreateKinematicSweepStateBoundingBoxAtAlpha(KinematicState, ImpactAlpha) };
    DirectX::BoundingOrientedBox DynamicBounds{ DynamicActor->GetWorldBoundingBox() };
    DynamicObb KinematicObb{ CreateDynamicObb(KinematicBounds) };
    DynamicObb DynamicObbValue{ CreateDynamicObb(DynamicBounds) };
    DynamicSatResult SatResult{};
    if (!ComputeObbSatResult(KinematicObb, DynamicObbValue, SatResult)) {
        SetActorTransformAtSweepAlpha(*DynamicActor, DynamicState, 1.0F);
        return false;
    }

    bool HasResolved{ ResolveKinematicDynamicCcdAtImpact(KinematicState, *DynamicActor, KinematicBounds, DynamicBounds, SatResult, ImpulseMagnitudeClamp) };
    if (!HasResolved) {
        SetActorTransformAtSweepAlpha(*DynamicActor, DynamicState, 1.0F);
        return false;
    }

    DynamicActor->UpdateSleepState(ResolveActorGravity(WorldMediator, *DynamicActor));
    return true;
}

void ResolveSweptKinematicDynamicCollisions(IPhysicsWorldMediator& WorldMediator, const std::vector<PhysicsKinematicActorSweepState>& KinematicSweepStates, const std::vector<DynamicActorSweepState>& DynamicSweepStates, float DeltaTime, float ImpulseMagnitudeClamp) {
    std::vector<KinematicDynamicSweepPairCandidate> PairCandidates{ QuerySweptKinematicDynamicCollisionPairs(KinematicSweepStates, DynamicSweepStates) };
    std::size_t PairCandidateCount{ PairCandidates.size() };
    for (std::size_t PairIndex{ 0U }; PairIndex < PairCandidateCount; ++PairIndex) {
        const PhysicsKinematicActorSweepState* KinematicState{ PairCandidates[PairIndex].mKinematicSweepState };
        const DynamicActorSweepState* DynamicState{ PairCandidates[PairIndex].mDynamicSweepState };
        if (KinematicState == nullptr || DynamicState == nullptr) {
            continue;
        }

        TryResolveSweptKinematicDynamicCollisionPair(WorldMediator, *KinematicState, *DynamicState, DeltaTime, ImpulseMagnitudeClamp);
    }
}

void ResolveStaticCollisions(IPhysicsWorldMediator& WorldMediator, const std::vector<PhysicsDynamicActor*>& DynamicActors, const std::vector<const PhysicsStaticActor*>& StaticActors, float DeltaTime) {
    std::size_t DynamicActorCount{ DynamicActors.size() };
    if (DynamicActorCount == 0U) {
        return;
    }

    std::size_t StaticActorCount{ StaticActors.size() };
    if (StaticActorCount == 0U) {
        return;
    }

    for (std::size_t DynamicActorIndex{ 0U }; DynamicActorIndex < DynamicActorCount; ++DynamicActorIndex) {
        PhysicsDynamicActor* DynamicActor{ DynamicActors[DynamicActorIndex] };
        if (DynamicActor == nullptr) {
            continue;
        }

        if (!DynamicActor->GetIsActive() || DynamicActor->GetInverseMass() <= 0.0F) {
            continue;
        }

        for (std::size_t StaticActorIndex{ 0U }; StaticActorIndex < StaticActorCount; ++StaticActorIndex) {
            const PhysicsStaticActor* StaticActor{ StaticActors[StaticActorIndex] };
            if (StaticActor == nullptr) {
                continue;
            }

            bool HasCollision{ StaticActor->ResolveDynamicCollision(*DynamicActor, DeltaTime) };
            if (!HasCollision) {
                continue;
            }

            WorldMediator.PublishEvent(PhysicsSimulationEventType::StaticCollisionResolved, DynamicActor, StaticActor);
            DynamicActor->UpdateSleepState(ResolveActorGravity(WorldMediator, *DynamicActor));
        }
    }
}

bool TryGetHighestTerrainSurfaceHeight(const IPhysicsActorRepository& ActorRepository, float WorldX, float WorldZ, float& OutSurfaceHeight) {
    std::vector<const PhysicsTerrainActor*> TerrainActors{};
    ActorRepository.CollectTerrainActors(TerrainActors);
    std::size_t TerrainActorCount{ TerrainActors.size() };
    bool HasSurfaceHeight{};
    float HighestSurfaceHeight{};

    for (std::size_t TerrainActorIndex{ 0U }; TerrainActorIndex < TerrainActorCount; ++TerrainActorIndex) {
        const PhysicsTerrainActor* TerrainActor{ TerrainActors[TerrainActorIndex] };
        if (TerrainActor == nullptr) {
            continue;
        }

        float SurfaceHeight{};
        bool HasCurrentSurfaceHeight{ TerrainActor->TryGetSurfaceHeightAtWorldPosition(WorldX, WorldZ, SurfaceHeight) };
        if (!HasCurrentSurfaceHeight) {
            continue;
        }

        if (!HasSurfaceHeight || SurfaceHeight > HighestSurfaceHeight) {
            HighestSurfaceHeight = SurfaceHeight;
            HasSurfaceHeight = true;
        }
    }

    if (!HasSurfaceHeight) {
        return false;
    }

    OutSurfaceHeight = HighestSurfaceHeight;
    return true;
}

float GetActorBottomOffsetFromPositionY(const PhysicsActorBase& Actor) {
    DirectX::XMFLOAT3 Corners[8]{};
    Actor.GetWorldBoundingBox().GetCorners(Corners);

    float MinimumY{ Corners[0].y };
    for (std::size_t CornerIndex{ 1U }; CornerIndex < 8U; ++CornerIndex) {
        if (Corners[CornerIndex].y < MinimumY) {
            MinimumY = Corners[CornerIndex].y;
        }
    }

    return MinimumY - Actor.GetPosition().y;
}

bool ResolveKinematicCharacterStaticContacts(IPhysicsActorRepository& ActorRepository, PhysicsActorBase& Actor, float DeltaTime) {
    bool HasResolved{};
    const std::size_t ActorCount{ ActorRepository.GetActorCount() };
    for (std::size_t ActorIndex{}; ActorIndex < ActorCount; ++ActorIndex) {
        PhysicsActorBase* OtherActor{ ActorRepository.GetActor(ActorIndex) };
        if (OtherActor == nullptr || OtherActor == &Actor || OtherActor->GetActorType() != PhysicsActorBase::PhysicsActorType::Static || OtherActor->IsTerrainActor() == true) {
            continue;
        }

        const bool IsResolved{ Actor.ResolveActorCollision(*OtherActor, DeltaTime) };
        HasResolved = HasResolved || IsResolved;
    }

    return HasResolved;
}

bool TrySnapKinematicCharacterToTerrain(const IPhysicsActorRepository& ActorRepository, PhysicsActorBase& Actor, float GroundSnapDistance) {
    if (Actor.HasFlag(PhysicsActorBase::PhysicsActorFlags::IgnoreTerrainCollide) || Actor.GetVelocity().y > 0.0F) {
        return false;
    }

    float SurfaceHeight{};
    const bool HasSurfaceHeight{ TryGetHighestTerrainSurfaceHeight(ActorRepository, Actor.GetPosition().x, Actor.GetPosition().z, SurfaceHeight) };
    if (HasSurfaceHeight == false) {
        return false;
    }

    const float ActorBottomOffsetFromPositionY{ GetActorBottomOffsetFromPositionY(Actor) };
    const float ActorBottomY{ Actor.GetPosition().y + ActorBottomOffsetFromPositionY };
    if (ActorBottomY - SurfaceHeight > GroundSnapDistance) {
        return false;
    }

    DirectX::SimpleMath::Vector3 NextPosition{ Actor.GetPosition() };
    NextPosition.y = SurfaceHeight - ActorBottomOffsetFromPositionY;
    Actor.SetPosition(NextPosition);
    return true;
}

bool ResolveKinematicActorTerrainContactInternal(const IPhysicsActorRepository& ActorRepository, PhysicsActorBase& Actor) {
    if (Actor.GetActorType() != PhysicsActorBase::PhysicsActorType::Kinematic || !Actor.GetIsActive()) {
        return false;
    }

    if (Actor.HasFlag(PhysicsActorBase::PhysicsActorFlags::IgnoreTerrainCollide)) {
        return false;
    }

    float SurfaceHeight{};
    bool HasSurfaceHeight{ TryGetHighestTerrainSurfaceHeight(ActorRepository, Actor.GetPosition().x, Actor.GetPosition().z, SurfaceHeight) };
    if (!HasSurfaceHeight) {
        return false;
    }

    DirectX::SimpleMath::Vector3 NextPosition{ Actor.GetPosition() };
    const float ActorBottomOffsetFromPositionY{ GetActorBottomOffsetFromPositionY(Actor) };
    const float ActorBottomY{ NextPosition.y + ActorBottomOffsetFromPositionY };
    if (ActorBottomY >= SurfaceHeight || Actor.GetVelocity().y >= 0.0F) {
        return false;
    }

    NextPosition.y = SurfaceHeight - ActorBottomOffsetFromPositionY;

    DirectX::SimpleMath::Vector3 NextVelocity{ Actor.GetVelocity() };
    NextVelocity.y = 0.0F;

    Actor.SetPosition(NextPosition);
    Actor.SetVelocity(NextVelocity);
    return true;
}
}

PhysicsFrameAccumulator::PhysicsFrameAccumulator()
    : mFixedTimeStep{ 1.0F / 60.0F },
      mAccumulatedTime{},
      mPreviousStates{},
      mCurrentStates{} {
}

PhysicsFrameAccumulator::~PhysicsFrameAccumulator() {
}

PhysicsFrameAccumulator::PhysicsFrameAccumulator(const PhysicsFrameAccumulator& Other)
    : mFixedTimeStep{ Other.mFixedTimeStep },
      mAccumulatedTime{ Other.mAccumulatedTime },
      mPreviousStates{ Other.mPreviousStates },
      mCurrentStates{ Other.mCurrentStates } {
}

PhysicsFrameAccumulator& PhysicsFrameAccumulator::operator=(const PhysicsFrameAccumulator& Other) {
    if (this == &Other) {
        return *this;
    }

    mFixedTimeStep = Other.mFixedTimeStep;
    mAccumulatedTime = Other.mAccumulatedTime;
    mPreviousStates = Other.mPreviousStates;
    mCurrentStates = Other.mCurrentStates;

    return *this;
}

PhysicsFrameAccumulator::PhysicsFrameAccumulator(PhysicsFrameAccumulator&& Other) noexcept
    : mFixedTimeStep{ Other.mFixedTimeStep },
      mAccumulatedTime{ Other.mAccumulatedTime },
      mPreviousStates{ std::move(Other.mPreviousStates) },
      mCurrentStates{ std::move(Other.mCurrentStates) } {
    Other.mFixedTimeStep = 1.0F / 60.0F;
    Other.mAccumulatedTime = 0.0F;
}

PhysicsFrameAccumulator& PhysicsFrameAccumulator::operator=(PhysicsFrameAccumulator&& Other) noexcept {
    if (this == &Other) {
        return *this;
    }

    mFixedTimeStep = Other.mFixedTimeStep;
    mAccumulatedTime = Other.mAccumulatedTime;
    mPreviousStates = std::move(Other.mPreviousStates);
    mCurrentStates = std::move(Other.mCurrentStates);

    Other.mFixedTimeStep = 1.0F / 60.0F;
    Other.mAccumulatedTime = 0.0F;

    return *this;
}

PhysicsFrameAccumulator::PhysicsFrameAccumulator(float FixedTimeStep)
    : mFixedTimeStep{ FixedTimeStep > 0.0F ? FixedTimeStep : (1.0F / 60.0F) },
      mAccumulatedTime{},
      mPreviousStates{},
      mCurrentStates{} {
}

void PhysicsFrameAccumulator::Initialize(float FixedTimeStep) {
    mFixedTimeStep = FixedTimeStep > 0.0F ? FixedTimeStep : (1.0F / 60.0F);
    mAccumulatedTime = 0.0F;
    mPreviousStates.clear();
    mCurrentStates.clear();
}

void PhysicsFrameAccumulator::AddDeltaTime(float DeltaTime) {
    if (DeltaTime <= 0.0F) {
        return;
    }

    mAccumulatedTime += DeltaTime;
}

bool PhysicsFrameAccumulator::TryConsumeFixedStep() {
    if (mFixedTimeStep <= 0.0F) {
        return false;
    }

    if (mAccumulatedTime < mFixedTimeStep) {
        return false;
    }

    mAccumulatedTime -= mFixedTimeStep;
    return true;
}

float PhysicsFrameAccumulator::GetAccumulatedTime() const {
    return mAccumulatedTime;
}

float PhysicsFrameAccumulator::GetInterpolationAlpha() const {
    if (mFixedTimeStep <= 0.0F) {
        return 0.0F;
    }

    float RawAlpha{ mAccumulatedTime / mFixedTimeStep };
    float InterpolationAlpha{ std::clamp(RawAlpha, 0.0F, 1.0F) };
    return InterpolationAlpha;
}

void PhysicsFrameAccumulator::SynchronizeStatePair(const IPhysicsActorRepository& ActorRepository) {
    CaptureState(mCurrentStates, ActorRepository);
    mPreviousStates = mCurrentStates;
}

void PhysicsFrameAccumulator::CapturePreviousState(const IPhysicsActorRepository& ActorRepository) {
    CaptureState(mPreviousStates, ActorRepository);
}

void PhysicsFrameAccumulator::CaptureCurrentState(const IPhysicsActorRepository& ActorRepository) {
    CaptureState(mCurrentStates, ActorRepository);
}

bool PhysicsFrameAccumulator::TryGetInterpolatedState(const PhysicsActorBase& Actor, DirectX::SimpleMath::Vector3& OutPosition, DirectX::SimpleMath::Quaternion& OutOrientation, DirectX::SimpleMath::Vector3& OutScale) const {
    ActorState PreviousState{};
    ActorState CurrentState{};
    bool HasPreviousState{ TryGetActorState(mPreviousStates, Actor, PreviousState) };
    bool HasCurrentState{ TryGetActorState(mCurrentStates, Actor, CurrentState) };
    if (!HasPreviousState && !HasCurrentState) {
        return false;
    }

    if (!HasPreviousState) {
        OutPosition = CurrentState.mPosition;
        OutOrientation = CurrentState.mOrientation;
        OutScale = CurrentState.mScale;
        return true;
    }

    if (!HasCurrentState) {
        OutPosition = PreviousState.mPosition;
        OutOrientation = PreviousState.mOrientation;
        OutScale = PreviousState.mScale;
        return true;
    }

    float InterpolationAlpha{ GetInterpolationAlpha() };
    OutPosition = InterpolateVector3(PreviousState.mPosition, CurrentState.mPosition, InterpolationAlpha);
    OutOrientation = InterpolateQuaternion(PreviousState.mOrientation, CurrentState.mOrientation, InterpolationAlpha);
    OutScale = InterpolateVector3(PreviousState.mScale, CurrentState.mScale, InterpolationAlpha);
    return true;
}

void PhysicsFrameAccumulator::CaptureState(std::vector<ActorState>& OutStates, const IPhysicsActorRepository& ActorRepository) const {
    OutStates.clear();

    std::size_t ActorCount{ ActorRepository.GetActorCount() };
    OutStates.reserve(ActorCount);
    for (std::size_t ActorIndex{ 0U }; ActorIndex < ActorCount; ++ActorIndex) {
        const PhysicsActorBase* ActorPointer{ ActorRepository.GetActor(ActorIndex) };
        if (ActorPointer == nullptr) {
            continue;
        }

        ActorState CapturedState{ CreateActorStateFromActor(*ActorPointer) };
        OutStates.push_back(CapturedState);
    }
}

bool PhysicsFrameAccumulator::TryGetActorState(const std::vector<ActorState>& States, const PhysicsActorBase& Actor, ActorState& OutActorState) const {
    std::size_t StateCount{ States.size() };
    for (std::size_t StateIndex{ 0U }; StateIndex < StateCount; ++StateIndex) {
        const ActorState& CurrentState{ States[StateIndex] };
        if (CurrentState.mActorPointer != &Actor) {
            continue;
        }

        OutActorState = CurrentState;
        return true;
    }

    return false;
}

PhysicsWorld::PhysicsWorld()
    : mSettings{ 1.0F / 60.0F, DirectX::SimpleMath::Vector3{ 0.0F, -9.8F, 0.0F } },
      mFrameAccumulator{ 1.0F / 60.0F },
      mLastUpdateStepCount{},
      mLastUpdateStepElapsedMilliseconds{},
      mLastStepElapsedMilliseconds{},
      mKinematicActorSimulator{},
      mActorRepository{},
      mSpatialQuery{},
      mPublishedEvents{},
      mDynamicActorScratch{},
      mIntegrateDynamicActorScratch{},
      mStaticActorScratch{},
      mKinematicActorScratch{} {
    InitializeDependencies();
    mKinematicActorSimulator.Synchronize(*mActorRepository);
    mFrameAccumulator.SynchronizeStatePair(*mActorRepository);
}

PhysicsWorld::~PhysicsWorld() {
}

PhysicsWorld::PhysicsWorld(const PhysicsWorld& Other)
    : IPhysicsWorld{ Other },
      mSettings{ Other.mSettings },
      mFrameAccumulator{ Other.mSettings.FixedTimeStep },
      mLastUpdateStepCount{ Other.mLastUpdateStepCount },
      mLastUpdateStepElapsedMilliseconds{ Other.mLastUpdateStepElapsedMilliseconds },
      mLastStepElapsedMilliseconds{ Other.mLastStepElapsedMilliseconds },
      mKinematicActorSimulator{ Other.mKinematicActorSimulator },
      mActorRepository{ Other.mActorRepository != nullptr ? Other.mActorRepository->Clone() : nullptr },
      mSpatialQuery{ Other.mSpatialQuery != nullptr ? Other.mSpatialQuery->Clone() : nullptr },
      mPublishedEvents{},
      mDynamicActorScratch{},
      mIntegrateDynamicActorScratch{},
      mStaticActorScratch{},
      mKinematicActorScratch{} {
    InitializeDependencies();
    mKinematicActorSimulator.Synchronize(*mActorRepository);
    mFrameAccumulator.Initialize(mSettings.FixedTimeStep);
    mFrameAccumulator.SynchronizeStatePair(*mActorRepository);
}

PhysicsWorld& PhysicsWorld::operator=(const PhysicsWorld& Other) {
    if (this == &Other) {
        return *this;
    }

    IPhysicsWorld::operator=(Other);
    mSettings = Other.mSettings;
    mLastUpdateStepCount = Other.mLastUpdateStepCount;
    mLastUpdateStepElapsedMilliseconds = Other.mLastUpdateStepElapsedMilliseconds;
    mLastStepElapsedMilliseconds = Other.mLastStepElapsedMilliseconds;
    mKinematicActorSimulator = Other.mKinematicActorSimulator;
    mActorRepository = Other.mActorRepository != nullptr ? Other.mActorRepository->Clone() : nullptr;
    mSpatialQuery = Other.mSpatialQuery != nullptr ? Other.mSpatialQuery->Clone() : nullptr;
    mPublishedEvents.clear();
    mDynamicActorScratch.clear();
    mIntegrateDynamicActorScratch.clear();
    mStaticActorScratch.clear();
    mKinematicActorScratch.clear();
    InitializeDependencies();
    mKinematicActorSimulator.Synchronize(*mActorRepository);
    mFrameAccumulator.Initialize(mSettings.FixedTimeStep);
    mFrameAccumulator.SynchronizeStatePair(*mActorRepository);

    return *this;
}

PhysicsWorld::PhysicsWorld(PhysicsWorld&& Other) noexcept
    : IPhysicsWorld{ std::move(Other) },
      mSettings{ Other.mSettings },
      mFrameAccumulator{ std::move(Other.mFrameAccumulator) },
      mLastUpdateStepCount{ Other.mLastUpdateStepCount },
      mLastUpdateStepElapsedMilliseconds{ Other.mLastUpdateStepElapsedMilliseconds },
      mLastStepElapsedMilliseconds{ Other.mLastStepElapsedMilliseconds },
      mKinematicActorSimulator{ std::move(Other.mKinematicActorSimulator) },
      mActorRepository{ std::move(Other.mActorRepository) },
      mSpatialQuery{ std::move(Other.mSpatialQuery) },
      mPublishedEvents{ std::move(Other.mPublishedEvents) },
      mDynamicActorScratch{},
      mIntegrateDynamicActorScratch{},
      mStaticActorScratch{},
      mKinematicActorScratch{} {
    if (mActorRepository != nullptr) {
        mFrameAccumulator.SynchronizeStatePair(*mActorRepository);
    }

    Other.mSettings = WorldSettings{ 1.0F / 60.0F, DirectX::SimpleMath::Vector3{} };
    Other.mLastUpdateStepCount = 0U;
    Other.mLastUpdateStepElapsedMilliseconds = 0.0;
    Other.mLastStepElapsedMilliseconds = 0.0;
    Other.mDynamicActorScratch.clear();
    Other.mIntegrateDynamicActorScratch.clear();
    Other.mStaticActorScratch.clear();
    Other.mKinematicActorScratch.clear();
    Other.InitializeDependencies();
    Other.mFrameAccumulator.Initialize(Other.mSettings.FixedTimeStep);
    Other.mKinematicActorSimulator.Synchronize(*Other.mActorRepository);
    Other.mFrameAccumulator.SynchronizeStatePair(*Other.mActorRepository);
    Other.ClearPublishedEvents();
}

PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&& Other) noexcept {
    if (this == &Other) {
        return *this;
    }

    IPhysicsWorld::operator=(std::move(Other));
    mSettings = Other.mSettings;
    mFrameAccumulator = std::move(Other.mFrameAccumulator);
    mLastUpdateStepCount = Other.mLastUpdateStepCount;
    mLastUpdateStepElapsedMilliseconds = Other.mLastUpdateStepElapsedMilliseconds;
    mLastStepElapsedMilliseconds = Other.mLastStepElapsedMilliseconds;
    mKinematicActorSimulator = std::move(Other.mKinematicActorSimulator);
    mActorRepository = std::move(Other.mActorRepository);
    mSpatialQuery = std::move(Other.mSpatialQuery);
    mPublishedEvents = std::move(Other.mPublishedEvents);
    mDynamicActorScratch.clear();
    mIntegrateDynamicActorScratch.clear();
    mStaticActorScratch.clear();
    mKinematicActorScratch.clear();
    if (mActorRepository != nullptr) {
        mFrameAccumulator.SynchronizeStatePair(*mActorRepository);
    }

    Other.mSettings = WorldSettings{ 1.0F / 60.0F, DirectX::SimpleMath::Vector3{} };
    Other.mLastUpdateStepCount = 0U;
    Other.mLastUpdateStepElapsedMilliseconds = 0.0;
    Other.mLastStepElapsedMilliseconds = 0.0;
    Other.mDynamicActorScratch.clear();
    Other.mIntegrateDynamicActorScratch.clear();
    Other.mStaticActorScratch.clear();
    Other.mKinematicActorScratch.clear();
    Other.InitializeDependencies();
    Other.mFrameAccumulator.Initialize(Other.mSettings.FixedTimeStep);
    Other.mKinematicActorSimulator.Synchronize(*Other.mActorRepository);
    Other.mFrameAccumulator.SynchronizeStatePair(*Other.mActorRepository);
    Other.ClearPublishedEvents();

    return *this;
}

PhysicsWorld::PhysicsWorld(const WorldSettings& Settings)
    : mSettings{ Settings },
      mFrameAccumulator{ Settings.FixedTimeStep },
      mLastUpdateStepCount{},
      mLastUpdateStepElapsedMilliseconds{},
      mLastStepElapsedMilliseconds{},
      mKinematicActorSimulator{},
      mActorRepository{},
      mSpatialQuery{},
      mPublishedEvents{},
      mDynamicActorScratch{},
      mIntegrateDynamicActorScratch{},
      mStaticActorScratch{},
      mKinematicActorScratch{} {
    InitializeDependencies();
    mKinematicActorSimulator.Synchronize(*mActorRepository);
    mFrameAccumulator.SynchronizeStatePair(*mActorRepository);
}

void PhysicsWorld::Initialize(const WorldSettings& Settings) {
    mSettings = Settings;
    mFrameAccumulator.Initialize(mSettings.FixedTimeStep);
    mLastUpdateStepCount = 0U;
    mLastUpdateStepElapsedMilliseconds = 0.0;
    mLastStepElapsedMilliseconds = 0.0;
    if (mActorRepository != nullptr) {
        mActorRepository->ClearActors();
    }

    mDynamicActorScratch.clear();
    mIntegrateDynamicActorScratch.clear();
    mStaticActorScratch.clear();
    mKinematicActorScratch.clear();

    if (mActorRepository != nullptr) {
        mFrameAccumulator.SynchronizeStatePair(*mActorRepository);
        mKinematicActorSimulator.Synchronize(*mActorRepository);
    }

    ClearPublishedEvents();
}

PhysicsDynamicActor* PhysicsWorld::CreateDynamicActor(const PhysicsDynamicActor::ActorDesc& Desc) {
    PhysicsDynamicActor* CreatedActor{ mActorRepository->CreateDynamicActor(Desc) };
    mFrameAccumulator.SynchronizeStatePair(*mActorRepository);
    return CreatedActor;
}

PhysicsKinematicActor* PhysicsWorld::CreateKinematicActor(const PhysicsKinematicActor::ActorDesc& Desc) {
    PhysicsKinematicActor* CreatedActor{ mActorRepository->CreateKinematicActor(Desc) };
    mKinematicActorSimulator.Synchronize(*mActorRepository);
    mFrameAccumulator.SynchronizeStatePair(*mActorRepository);
    return CreatedActor;
}

PhysicsTerrainActor* PhysicsWorld::CreateTerrainActor(const PhysicsTerrainActor::ActorDesc& Desc) {
    PhysicsTerrainActor* CreatedActor{ mActorRepository->CreateTerrainActor(Desc) };
    mFrameAccumulator.SynchronizeStatePair(*mActorRepository);
    return CreatedActor;
}

void PhysicsWorld::AddActor(std::unique_ptr<PhysicsActorBase> Actor) {
    mActorRepository->AddActor(std::move(Actor));
    mKinematicActorSimulator.Synchronize(*mActorRepository);
    mFrameAccumulator.SynchronizeStatePair(*mActorRepository);
}

void PhysicsWorld::ClearActors() {
    mActorRepository->ClearActors();
    mDynamicActorScratch.clear();
    mIntegrateDynamicActorScratch.clear();
    mStaticActorScratch.clear();
    mKinematicActorScratch.clear();
    mKinematicActorSimulator.Synchronize(*mActorRepository);
    mFrameAccumulator.SynchronizeStatePair(*mActorRepository);
}

PhysicsActorBase* PhysicsWorld::GetActor(std::size_t Index) {
    return mActorRepository->GetActor(Index);
}

const PhysicsActorBase* PhysicsWorld::GetActor(std::size_t Index) const {
    return mActorRepository->GetActor(Index);
}

PhysicsTerrainActor* PhysicsWorld::GetTerrainActor(std::size_t Index) {
    PhysicsTerrainActor* TerrainActorPointer{ mActorRepository->GetTerrainActor(Index) };
    return TerrainActorPointer;
}

const PhysicsTerrainActor* PhysicsWorld::GetTerrainActor(std::size_t Index) const {
    const IPhysicsActorRepository& ActorRepository{ *mActorRepository };
    const PhysicsTerrainActor* TerrainActorPointer{ ActorRepository.GetTerrainActor(Index) };
    return TerrainActorPointer;
}

std::size_t PhysicsWorld::GetActorCount() const {
    std::size_t ActorCount{ mActorRepository->GetActorCount() };
    return ActorCount;
}

std::vector<PhysicsTerrainActor*> PhysicsWorld::CollectTerrainActors() {
    std::vector<PhysicsTerrainActor*> TerrainActors{};
    mActorRepository->CollectTerrainActors(TerrainActors);
    return TerrainActors;
}

std::vector<const PhysicsTerrainActor*> PhysicsWorld::CollectTerrainActors() const {
    const IPhysicsActorRepository& ActorRepository{ *mActorRepository };
    std::vector<const PhysicsTerrainActor*> TerrainActors{};
    ActorRepository.CollectTerrainActors(TerrainActors);
    return TerrainActors;
}

const PhysicsWorld::WorldSettings& PhysicsWorld::GetSettings() const {
    return mSettings;
}

float PhysicsWorld::GetAccumulator() const {
    return mFrameAccumulator.GetAccumulatedTime();
}

float PhysicsWorld::GetInterpolationAlpha() const {
    return mFrameAccumulator.GetInterpolationAlpha();
}

std::size_t PhysicsWorld::GetLastUpdateStepCount() const {
    return mLastUpdateStepCount;
}

double PhysicsWorld::GetLastUpdateStepElapsedMilliseconds() const {
    return mLastUpdateStepElapsedMilliseconds;
}

double PhysicsWorld::GetLastStepElapsedMilliseconds() const {
    return mLastStepElapsedMilliseconds;
}

bool PhysicsWorld::TryGetInterpolatedActorTransform(const PhysicsActorBase& Actor, DirectX::SimpleMath::Vector3& OutPosition, DirectX::SimpleMath::Quaternion& OutOrientation, DirectX::SimpleMath::Vector3& OutScale) const {
    bool HasInterpolatedState{ mFrameAccumulator.TryGetInterpolatedState(Actor, OutPosition, OutOrientation, OutScale) };
    return HasInterpolatedState;
}

void PhysicsWorld::TickKinematicActors(float DeltaTime) {
    mKinematicActorSimulator.Tick(*this, *mActorRepository, DeltaTime);
}

void PhysicsWorld::MarkKinematicActorTeleported(const PhysicsKinematicActor& Actor) {
    mKinematicActorSimulator.MarkTeleported(Actor);
}

void PhysicsWorld::StepSimulation() {
    ClearPublishedEvents();
    IPhysicsActorRepository& ActorRepository{ GetActorRepository() };
    ActorRepository.CollectDynamicActors(mDynamicActorScratch);
    ActorRepository.CollectStaticActors(mStaticActorScratch);
    std::size_t DynamicActorCount{ mDynamicActorScratch.size() };
    for (std::size_t ActorIndex{ 0U }; ActorIndex < DynamicActorCount; ++ActorIndex) {
        PhysicsDynamicActor* DynamicActor{ mDynamicActorScratch[ActorIndex] };
        if (DynamicActor == nullptr) {
            continue;
        }

        DynamicActor->ClearContactState();
    }

    std::vector<PhysicsDynamicCollisionPairCandidate> PreviousDynamicPairCandidates{ GetSpatialQuery().QueryDynamicCollisionPairs(ActorRepository) };
    ResolveDynamicCollisions(*this, PreviousDynamicPairCandidates, mSettings.FixedTimeStep);
    ResolveStaticCollisions(*this, mDynamicActorScratch, mStaticActorScratch, mSettings.FixedTimeStep);
    ResolveKinematicCollisions(ActorRepository, mSettings.Gravity, mSettings.FixedTimeStep);

    std::vector<DynamicActorSweepState> DynamicActorSweepStates{ CaptureDynamicActorSweepStates(mDynamicActorScratch) };
    const std::vector<PhysicsKinematicActorSweepState>& KinematicActorSweepStates{ mKinematicActorSimulator.GetSweepStates() };
    IntegrateDynamicActors(*this, ActorRepository, mIntegrateDynamicActorScratch, mSettings.FixedTimeStep);

    CompleteDynamicActorSweepStates(DynamicActorSweepStates);
    std::vector<PhysicsDynamicCollisionPairCandidate> SweptDynamicPairCandidates{ QuerySweptDynamicCollisionPairs(DynamicActorSweepStates) };
    ResolveSweptDynamicCollisions(*this, DynamicActorSweepStates, SweptDynamicPairCandidates, mSettings.FixedTimeStep);
    ResolveSweptKinematicDynamicCollisions(*this, KinematicActorSweepStates, DynamicActorSweepStates, mSettings.FixedTimeStep, mSettings.KinematicDynamicCcdImpulseMagnitudeClamp);

    std::vector<PhysicsDynamicCollisionPairCandidate> CurrentDynamicPairCandidates{ GetSpatialQuery().QueryDynamicCollisionPairs(ActorRepository) };
    ResolveDynamicCollisions(*this, CurrentDynamicPairCandidates, mSettings.FixedTimeStep);
    ResolveStaticCollisions(*this, mDynamicActorScratch, mStaticActorScratch, mSettings.FixedTimeStep);
    ResolveKinematicCollisions(ActorRepository, mSettings.Gravity, mSettings.FixedTimeStep);
    mKinematicActorSimulator.MarkSweepStatesConsumed();
}

void PhysicsWorld::Update(float DeltaTime) {
    mLastUpdateStepCount = 0U;
    mLastUpdateStepElapsedMilliseconds = 0.0;
    mLastStepElapsedMilliseconds = 0.0;
    mFrameAccumulator.AddDeltaTime(DeltaTime);

    while (mFrameAccumulator.TryConsumeFixedStep()) {
        mFrameAccumulator.CapturePreviousState(*mActorRepository);
        std::chrono::steady_clock::time_point StepStartTime{ std::chrono::steady_clock::now() };
        StepSimulation();
        std::chrono::steady_clock::time_point StepEndTime{ std::chrono::steady_clock::now() };
        mFrameAccumulator.CaptureCurrentState(*mActorRepository);
        std::chrono::duration<double, std::milli> StepElapsedTime{ StepEndTime - StepStartTime };
        mLastUpdateStepElapsedMilliseconds += StepElapsedTime.count();
        mLastStepElapsedMilliseconds = StepElapsedTime.count();
        ++mLastUpdateStepCount;
    }
}

PhysicsCharacterMoveResult PhysicsWorld::MoveKinematicCharacter(PhysicsActorBase& Actor, const PhysicsCharacterMoveRequest& Request, float DeltaTime) {
    PhysicsCharacterMoveResult Result{};
    Result.mPosition = Actor.GetPosition();
    Result.mVelocity = Actor.GetVelocity();
    if (Actor.GetActorType() != PhysicsActorBase::PhysicsActorType::Kinematic || Actor.GetIsActive() == false || DeltaTime <= 0.0F) {
        return Result;
    }

    const DirectX::SimpleMath::Vector3 StartingPosition{ Actor.GetPosition() };
    const float DisplacementLength{ Request.mDisplacement.Length() };
    const std::size_t StepCount{ std::clamp(static_cast<std::size_t>(std::ceil(DisplacementLength / 0.25F)), std::size_t{ 1U }, std::size_t{ 8U }) };
    const DirectX::SimpleMath::Vector3 StepDisplacement{ Request.mDisplacement / static_cast<float>(StepCount) };
    const float StepDeltaTime{ DeltaTime / static_cast<float>(StepCount) };
    const float GroundSnapDistance{ std::max(0.0F, Request.mGroundSnapDistance) };

    bool IsGrounded{};
    for (std::size_t StepIndex{}; StepIndex < StepCount; ++StepIndex) {
        Actor.SetPosition(Actor.GetPosition() + StepDisplacement);
        static_cast<void>(ResolveKinematicCharacterStaticContacts(*mActorRepository, Actor, StepDeltaTime));
        const bool IsGroundedAtStep{ TrySnapKinematicCharacterToTerrain(*mActorRepository, Actor, GroundSnapDistance) };
        IsGrounded = IsGroundedAtStep;
    }

    DirectX::SimpleMath::Vector3 FinalVelocity{ (Actor.GetPosition() - StartingPosition) / DeltaTime };
    if (IsGrounded == true) {
        FinalVelocity.y = 0.0F;
    }

    Actor.SetVelocity(FinalVelocity);

    Result.mPosition = Actor.GetPosition();
    Result.mVelocity = FinalVelocity;
    Result.mIsGrounded = IsGrounded;
    return Result;
}

bool PhysicsWorld::ResolveKinematicTerrainContact(PhysicsActorBase& Actor) {
    bool HasResolved{ ResolveKinematicActorTerrainContactInternal(*mActorRepository, Actor) };
    return HasResolved;
}

void PhysicsWorld::ResolveKinematicTerrainContacts() {
    IPhysicsActorRepository& ActorRepository{ GetActorRepository() };
    ActorRepository.CollectKinematicActors(mKinematicActorScratch);
    std::size_t KinematicActorCount{ mKinematicActorScratch.size() };
    for (std::size_t ActorIndex{ 0U }; ActorIndex < KinematicActorCount; ++ActorIndex) {
        PhysicsKinematicActor* KinematicActor{ mKinematicActorScratch[ActorIndex] };
        if (KinematicActor == nullptr) {
            continue;
        }

        ResolveKinematicActorTerrainContactInternal(ActorRepository, *KinematicActor);
    }
}

const DirectX::SimpleMath::Vector3& PhysicsWorld::GetGravity() const {
    return mSettings.Gravity;
}

IPhysicsActorRepository& PhysicsWorld::GetActorRepository() {
    return *mActorRepository;
}

const IPhysicsActorRepository& PhysicsWorld::GetActorRepository() const {
    return *mActorRepository;
}

IPhysicsSpatialQuery& PhysicsWorld::GetSpatialQuery() {
    return *mSpatialQuery;
}

const IPhysicsSpatialQuery& PhysicsWorld::GetSpatialQuery() const {
    return *mSpatialQuery;
}

void PhysicsWorld::PublishEvent(PhysicsSimulationEventType EventType, const PhysicsActorBase* FirstActor, const PhysicsActorBase* SecondActor) {
    mPublishedEvents.push_back(PhysicsSimulationEvent{ EventType, FirstActor, SecondActor });
}

void PhysicsWorld::ClearPublishedEvents() {
    mPublishedEvents.clear();
}

const std::vector<PhysicsSimulationEvent>& PhysicsWorld::GetPublishedEvents() const {
    return mPublishedEvents;
}

void PhysicsWorld::InitializeDependencies() {
    if (mActorRepository == nullptr) {
        mActorRepository = std::make_unique<PhysicsActorRepository>();
    }

    if (mSpatialQuery == nullptr) {
        mSpatialQuery = std::make_unique<BruteForcePhysicsSpatialQuery>();
    }
}
