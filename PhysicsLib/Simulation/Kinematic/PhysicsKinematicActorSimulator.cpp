#include "PhysicsLib/Simulation/Kinematic/PhysicsKinematicActorSimulator.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "PhysicsLib/Actors/PhysicsKinematicActor.h"
#include "PhysicsLib/Simulation/Mediator/IPhysicsWorldMediator.h"
#include "PhysicsLib/Simulation/Repository/IPhysicsActorRepository.h"

#undef max
#undef min

namespace {
constexpr float KinematicSweepVelocityDeltaTimeEpsilon{ 0.00001F };

DirectX::BoundingOrientedBox CreateKinematicActorBoundingBoxAtTransform(const PhysicsActorBase& Actor, const DirectX::SimpleMath::Vector3& Position, const DirectX::SimpleMath::Quaternion& Orientation, const DirectX::SimpleMath::Vector3& Scale) {
    DirectX::SimpleMath::Matrix ScalingMatrix{ DirectX::SimpleMath::Matrix::CreateScale(Scale) };
    DirectX::SimpleMath::Matrix RotationMatrix{ DirectX::SimpleMath::Matrix::CreateFromQuaternion(Orientation) };
    DirectX::SimpleMath::Matrix TranslationMatrix{ DirectX::SimpleMath::Matrix::CreateTranslation(Position) };
    DirectX::SimpleMath::Matrix WorldMatrix{ ScalingMatrix * RotationMatrix * TranslationMatrix };
    DirectX::BoundingOrientedBox WorldBoundingBox{};
    Actor.GetLocalBoundingBox().Transform(WorldBoundingBox, WorldMatrix);
    return WorldBoundingBox;
}

PhysicsKinematicActorSweepBounds MakeKinematicSweepBounds(const DirectX::BoundingOrientedBox& BoundingBox) {
    DirectX::XMFLOAT3 Corners[8]{};
    BoundingBox.GetCorners(Corners);

    PhysicsKinematicActorSweepBounds Bounds{};
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

PhysicsKinematicActorSweepBounds MergeKinematicSweepBounds(const PhysicsKinematicActorSweepBounds& FirstBounds, const PhysicsKinematicActorSweepBounds& SecondBounds) {
    PhysicsKinematicActorSweepBounds MergedBounds{};
    MergedBounds.mMinimum.x = std::min(FirstBounds.mMinimum.x, SecondBounds.mMinimum.x);
    MergedBounds.mMinimum.y = std::min(FirstBounds.mMinimum.y, SecondBounds.mMinimum.y);
    MergedBounds.mMinimum.z = std::min(FirstBounds.mMinimum.z, SecondBounds.mMinimum.z);
    MergedBounds.mMaximum.x = std::max(FirstBounds.mMaximum.x, SecondBounds.mMaximum.x);
    MergedBounds.mMaximum.y = std::max(FirstBounds.mMaximum.y, SecondBounds.mMaximum.y);
    MergedBounds.mMaximum.z = std::max(FirstBounds.mMaximum.z, SecondBounds.mMaximum.z);
    return MergedBounds;
}

const PhysicsKinematicActorSweepState* FindSweepStateInStates(const std::vector<PhysicsKinematicActorSweepState>& SweepStates, const PhysicsKinematicActor& Actor) {
    std::size_t SweepStateCount{ SweepStates.size() };
    for (std::size_t SweepStateIndex{ 0U }; SweepStateIndex < SweepStateCount; ++SweepStateIndex) {
        const PhysicsKinematicActorSweepState& SweepState{ SweepStates[SweepStateIndex] };
        if (SweepState.mActor == &Actor) {
            return &SweepState;
        }
    }

    return nullptr;
}
}

PhysicsKinematicActorSimulator::PhysicsKinematicActorSimulator() = default;

PhysicsKinematicActorSimulator::~PhysicsKinematicActorSimulator() = default;

PhysicsKinematicActorSimulator::PhysicsKinematicActorSimulator(const PhysicsKinematicActorSimulator& Other) = default;

PhysicsKinematicActorSimulator& PhysicsKinematicActorSimulator::operator=(const PhysicsKinematicActorSimulator& Other) = default;

PhysicsKinematicActorSimulator::PhysicsKinematicActorSimulator(PhysicsKinematicActorSimulator&& Other) noexcept = default;

PhysicsKinematicActorSimulator& PhysicsKinematicActorSimulator::operator=(PhysicsKinematicActorSimulator&& Other) noexcept = default;

void PhysicsKinematicActorSimulator::Tick(IPhysicsWorldMediator& WorldMediator, IPhysicsActorRepository& ActorRepository, float DeltaTime) {
    (void)WorldMediator;

    if (DeltaTime <= 0.0F) {
        return;
    }

    std::vector<PhysicsKinematicActorSweepState> PreviousSweepStates{ std::move(mSweepStates) };
    mSweepStates.clear();

    ActorRepository.CollectKinematicActors(mKinematicActorScratch);
    mSweepStates.reserve(mKinematicActorScratch.size());
    std::size_t KinematicActorCount{ mKinematicActorScratch.size() };
    for (std::size_t ActorIndex{ 0U }; ActorIndex < KinematicActorCount; ++ActorIndex) {
        PhysicsKinematicActor* KinematicActor{ mKinematicActorScratch[ActorIndex] };
        if (KinematicActor == nullptr) {
            continue;
        }

        const PhysicsKinematicActorSweepState* PreviousSweepState{ FindSweepStateInStates(PreviousSweepStates, *KinematicActor) };
        bool IsTeleport{ IsMarkedTeleported(*KinematicActor) };

        PhysicsKinematicActorSweepState SweepState{};
        SweepState.mActor = KinematicActor;
        SweepState.mPreviousPosition = KinematicActor->GetPosition();
        SweepState.mPreviousOrientation = KinematicActor->GetOrientation();
        SweepState.mPreviousScale = KinematicActor->GetScale();
        SweepState.mDeltaTime = DeltaTime;

        if (PreviousSweepState != nullptr && !IsTeleport) {
            SweepState.mPreviousPosition = PreviousSweepState->mCurrentPosition;
            SweepState.mPreviousOrientation = PreviousSweepState->mCurrentOrientation;
            SweepState.mPreviousScale = PreviousSweepState->mCurrentScale;
        }

        SweepState.mCurrentPosition = KinematicActor->GetPosition();
        SweepState.mCurrentOrientation = KinematicActor->GetOrientation();
        SweepState.mCurrentScale = KinematicActor->GetScale();
        SweepState.mMovementDelta = SweepState.mCurrentPosition - SweepState.mPreviousPosition;
        SweepState.mVelocity = KinematicActor->GetVelocity();
        if (SweepState.mVelocity.LengthSquared() <= 0.0F && SweepState.mDeltaTime > KinematicSweepVelocityDeltaTimeEpsilon) {
            SweepState.mVelocity = SweepState.mMovementDelta / SweepState.mDeltaTime;
        }

        DirectX::BoundingOrientedBox PreviousBounds{ CreateKinematicActorBoundingBoxAtTransform(*KinematicActor, SweepState.mPreviousPosition, SweepState.mPreviousOrientation, SweepState.mPreviousScale) };
        DirectX::BoundingOrientedBox CurrentBounds{ CreateKinematicActorBoundingBoxAtTransform(*KinematicActor, SweepState.mCurrentPosition, SweepState.mCurrentOrientation, SweepState.mCurrentScale) };
        SweepState.mPreviousBounds = MakeKinematicSweepBounds(PreviousBounds);
        SweepState.mCurrentBounds = MakeKinematicSweepBounds(CurrentBounds);
        SweepState.mSweptBounds = MergeKinematicSweepBounds(SweepState.mPreviousBounds, SweepState.mCurrentBounds);
        SweepState.mIsTeleport = IsTeleport;
        SweepState.mCanUseForCcd = !IsTeleport;
        mSweepStates.push_back(SweepState);
        ClearTeleportMark(*KinematicActor);
    }
}

void PhysicsKinematicActorSimulator::Synchronize(IPhysicsActorRepository& ActorRepository) {
    mSweepStates.clear();
    mTeleportedActors.clear();

    ActorRepository.CollectKinematicActors(mKinematicActorScratch);
    mSweepStates.reserve(mKinematicActorScratch.size());
    std::size_t KinematicActorCount{ mKinematicActorScratch.size() };
    for (std::size_t ActorIndex{ 0U }; ActorIndex < KinematicActorCount; ++ActorIndex) {
        PhysicsKinematicActor* KinematicActor{ mKinematicActorScratch[ActorIndex] };
        if (KinematicActor == nullptr) {
            continue;
        }

        PhysicsKinematicActorSweepState SweepState{};
        SweepState.mActor = KinematicActor;
        SweepState.mPreviousPosition = KinematicActor->GetPosition();
        SweepState.mPreviousOrientation = KinematicActor->GetOrientation();
        SweepState.mPreviousScale = KinematicActor->GetScale();
        SweepState.mCurrentPosition = SweepState.mPreviousPosition;
        SweepState.mCurrentOrientation = SweepState.mPreviousOrientation;
        SweepState.mCurrentScale = SweepState.mPreviousScale;

        DirectX::BoundingOrientedBox CurrentBounds{ KinematicActor->GetWorldBoundingBox() };
        SweepState.mPreviousBounds = MakeKinematicSweepBounds(CurrentBounds);
        SweepState.mCurrentBounds = SweepState.mPreviousBounds;
        SweepState.mSweptBounds = SweepState.mPreviousBounds;
        SweepState.mMovementDelta = DirectX::SimpleMath::Vector3{};
        SweepState.mVelocity = DirectX::SimpleMath::Vector3{};
        SweepState.mDeltaTime = 0.0F;
        SweepState.mIsTeleport = false;
        SweepState.mCanUseForCcd = false;
        mSweepStates.push_back(SweepState);
    }
}

void PhysicsKinematicActorSimulator::MarkTeleported(const PhysicsKinematicActor& Actor) {
    if (IsMarkedTeleported(Actor)) {
        return;
    }

    mTeleportedActors.push_back(&Actor);
}

void PhysicsKinematicActorSimulator::MarkSweepStatesConsumed() {
    std::size_t SweepStateCount{ mSweepStates.size() };
    for (std::size_t SweepStateIndex{ 0U }; SweepStateIndex < SweepStateCount; ++SweepStateIndex) {
        PhysicsKinematicActorSweepState& SweepState{ mSweepStates[SweepStateIndex] };
        if (SweepState.mActor != nullptr) {
            SweepState.mCurrentPosition = SweepState.mActor->GetPosition();
            SweepState.mCurrentOrientation = SweepState.mActor->GetOrientation();
            SweepState.mCurrentScale = SweepState.mActor->GetScale();
            DirectX::BoundingOrientedBox CurrentBounds{ SweepState.mActor->GetWorldBoundingBox() };
            SweepState.mCurrentBounds = MakeKinematicSweepBounds(CurrentBounds);
        }

        SweepState.mPreviousPosition = SweepState.mCurrentPosition;
        SweepState.mPreviousOrientation = SweepState.mCurrentOrientation;
        SweepState.mPreviousScale = SweepState.mCurrentScale;
        SweepState.mPreviousBounds = SweepState.mCurrentBounds;
        SweepState.mSweptBounds = SweepState.mCurrentBounds;
        SweepState.mMovementDelta = DirectX::SimpleMath::Vector3{};
        SweepState.mVelocity = DirectX::SimpleMath::Vector3{};
        SweepState.mDeltaTime = 0.0F;
        SweepState.mIsTeleport = false;
        SweepState.mCanUseForCcd = false;
    }

    mTeleportedActors.clear();
}

const std::vector<PhysicsKinematicActorSweepState>& PhysicsKinematicActorSimulator::GetSweepStates() const {
    return mSweepStates;
}

bool PhysicsKinematicActorSimulator::IsMarkedTeleported(const PhysicsKinematicActor& Actor) const {
    std::size_t TeleportedActorCount{ mTeleportedActors.size() };
    for (std::size_t ActorIndex{ 0U }; ActorIndex < TeleportedActorCount; ++ActorIndex) {
        if (mTeleportedActors[ActorIndex] == &Actor) {
            return true;
        }
    }

    return false;
}

void PhysicsKinematicActorSimulator::ClearTeleportMark(const PhysicsKinematicActor& Actor) {
    auto NewEndIterator{ std::remove(mTeleportedActors.begin(), mTeleportedActors.end(), &Actor) };
    mTeleportedActors.erase(NewEndIterator, mTeleportedActors.end());
}
