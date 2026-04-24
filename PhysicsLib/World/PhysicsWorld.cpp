#include "PhysicsWorld.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>

#include "PhysicsLib/Simulation/Repository/IPhysicsActorRepository.h"
#include "PhysicsLib/Simulation/Repository/PhysicsActorRepository.h"
#include "PhysicsLib/Simulation/SpatialQuery/BruteForcePhysicsSpatialQuery.h"
#include "PhysicsLib/Simulation/SpatialQuery/IPhysicsSpatialQuery.h"

namespace {
constexpr std::size_t DynamicCollisionSolverMinimumIterationCount{ 1U };
constexpr std::size_t DynamicCollisionSolverMaximumIterationCount{ 4U };
constexpr std::size_t DynamicCollisionPairsPerAdditionalIteration{ 24U };

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

PhysicsFrameAccumulator::ActorState CreateActorStateFromActor(const PhysicsActorBase& Actor) {
    PhysicsFrameAccumulator::ActorState ActorStateValue{ &Actor, Actor.GetActorType(), Actor.GetPosition(), Actor.GetOrientation(), Actor.GetScale() };
    return ActorStateValue;
}

void IntegrateDynamicActors(IPhysicsWorldMediator& WorldMediator, IPhysicsActorRepository& ActorRepository, float DeltaTime) {
    std::vector<PhysicsDynamicActor*> DynamicActors{ ActorRepository.CollectDynamicActors() };
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

void IntegrateKinematicActors(IPhysicsWorldMediator& WorldMediator, IPhysicsActorRepository& ActorRepository, float DeltaTime) {
    std::vector<PhysicsKinematicActor*> KinematicActors{ ActorRepository.CollectKinematicActors() };
    std::size_t KinematicActorCount{ KinematicActors.size() };
    for (std::size_t ActorIndex{ 0U }; ActorIndex < KinematicActorCount; ++ActorIndex) {
        PhysicsKinematicActor* KinematicActor{ KinematicActors[ActorIndex] };
        if (KinematicActor == nullptr) {
            continue;
        }

        KinematicActor->Integrate(WorldMediator, DeltaTime);
        KinematicActor->SolveConstraints(WorldMediator, DeltaTime);
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
                bool HasCollision{ FirstActor->ResolveActorCollision(*SecondActor, DeltaTime) };
                if (HasCollision && SecondActor->GetActorType() == PhysicsActorBase::PhysicsActorType::Dynamic) {
                    SecondActor->UpdateSleepState(Gravity);
                }

                continue;
            }

            if (SecondActor->GetActorType() == PhysicsActorBase::PhysicsActorType::Kinematic) {
                bool HasCollision{ SecondActor->ResolveActorCollision(*FirstActor, DeltaTime) };
                if (HasCollision && FirstActor->GetActorType() == PhysicsActorBase::PhysicsActorType::Dynamic) {
                    FirstActor->UpdateSleepState(Gravity);
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
            FirstActor->UpdateSleepState(WorldMediator.GetGravity());
            SecondActor->UpdateSleepState(WorldMediator.GetGravity());
        }

        if (!HasAnyCollision) {
            break;
        }
    }

    PhysicsDynamicCollisionSolver::EndFrame();
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
            DynamicActor->UpdateSleepState(WorldMediator.GetGravity());
        }
    }
}

bool TryGetHighestTerrainSurfaceHeight(const IPhysicsActorRepository& ActorRepository, float WorldX, float WorldZ, float& OutSurfaceHeight) {
    std::vector<const PhysicsTerrainActor*> TerrainActors{ ActorRepository.CollectTerrainActors() };
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

bool ResolveKinematicActorTerrainContactInternal(const IPhysicsActorRepository& ActorRepository, PhysicsActorBase& Actor) {
    if (Actor.GetActorType() != PhysicsActorBase::PhysicsActorType::Kinematic || !Actor.GetIsActive()) {
        return false;
    }

    if (!Actor.HasFlag(PhysicsActorBase::PhysicsActorFlags::TerrainCollide)) {
        return false;
    }

    float SurfaceHeight{};
    bool HasSurfaceHeight{ TryGetHighestTerrainSurfaceHeight(ActorRepository, Actor.GetPosition().x, Actor.GetPosition().z, SurfaceHeight) };
    if (!HasSurfaceHeight) {
        return false;
    }

    DirectX::SimpleMath::Vector3 NextPosition{ Actor.GetPosition() };
    const float ActorBottomOffsetFromPositionY{ GetActorBottomOffsetFromPositionY(Actor) };
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
      mActorRepository{},
      mSpatialQuery{},
      mPublishedEvents{} {
    InitializeDependencies();
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
      mActorRepository{ Other.mActorRepository != nullptr ? Other.mActorRepository->Clone() : nullptr },
      mSpatialQuery{ Other.mSpatialQuery != nullptr ? Other.mSpatialQuery->Clone() : nullptr },
      mPublishedEvents{} {
    InitializeDependencies();
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
    mActorRepository = Other.mActorRepository != nullptr ? Other.mActorRepository->Clone() : nullptr;
    mSpatialQuery = Other.mSpatialQuery != nullptr ? Other.mSpatialQuery->Clone() : nullptr;
    mPublishedEvents.clear();
    InitializeDependencies();
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
      mActorRepository{ std::move(Other.mActorRepository) },
      mSpatialQuery{ std::move(Other.mSpatialQuery) },
      mPublishedEvents{ std::move(Other.mPublishedEvents) } {
    if (mActorRepository != nullptr) {
        mFrameAccumulator.SynchronizeStatePair(*mActorRepository);
    }

    Other.mSettings = WorldSettings{ 1.0F / 60.0F, DirectX::SimpleMath::Vector3{} };
    Other.mLastUpdateStepCount = 0U;
    Other.mLastUpdateStepElapsedMilliseconds = 0.0;
    Other.mLastStepElapsedMilliseconds = 0.0;
    Other.InitializeDependencies();
    Other.mFrameAccumulator.Initialize(Other.mSettings.FixedTimeStep);
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
    mActorRepository = std::move(Other.mActorRepository);
    mSpatialQuery = std::move(Other.mSpatialQuery);
    mPublishedEvents = std::move(Other.mPublishedEvents);
    if (mActorRepository != nullptr) {
        mFrameAccumulator.SynchronizeStatePair(*mActorRepository);
    }

    Other.mSettings = WorldSettings{ 1.0F / 60.0F, DirectX::SimpleMath::Vector3{} };
    Other.mLastUpdateStepCount = 0U;
    Other.mLastUpdateStepElapsedMilliseconds = 0.0;
    Other.mLastStepElapsedMilliseconds = 0.0;
    Other.InitializeDependencies();
    Other.mFrameAccumulator.Initialize(Other.mSettings.FixedTimeStep);
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
      mActorRepository{},
      mSpatialQuery{},
      mPublishedEvents{} {
    InitializeDependencies();
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

    if (mActorRepository != nullptr) {
        mFrameAccumulator.SynchronizeStatePair(*mActorRepository);
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
    mFrameAccumulator.SynchronizeStatePair(*mActorRepository);
}

void PhysicsWorld::ClearActors() {
    mActorRepository->ClearActors();
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
    std::vector<PhysicsTerrainActor*> TerrainActors{ mActorRepository->CollectTerrainActors() };
    return TerrainActors;
}

std::vector<const PhysicsTerrainActor*> PhysicsWorld::CollectTerrainActors() const {
    const IPhysicsActorRepository& ActorRepository{ *mActorRepository };
    std::vector<const PhysicsTerrainActor*> TerrainActors{ ActorRepository.CollectTerrainActors() };
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

void PhysicsWorld::StepSimulation() {
    ClearPublishedEvents();
    IPhysicsActorRepository& ActorRepository{ GetActorRepository() };
    std::vector<PhysicsDynamicCollisionPairCandidate> PairCandidates{ GetSpatialQuery().QueryDynamicCollisionPairs(ActorRepository) };
    std::vector<PhysicsDynamicActor*> DynamicActors{ ActorRepository.CollectDynamicActors() };
    std::vector<const PhysicsStaticActor*> StaticActors{ ActorRepository.CollectStaticActors() };
    std::size_t DynamicActorCount{ DynamicActors.size() };
    for (std::size_t ActorIndex{ 0U }; ActorIndex < DynamicActorCount; ++ActorIndex) {
        PhysicsDynamicActor* DynamicActor{ DynamicActors[ActorIndex] };
        if (DynamicActor == nullptr) {
            continue;
        }

        DynamicActor->ClearContactState();
    }

    IntegrateKinematicActors(*this, ActorRepository, mSettings.FixedTimeStep);
    ResolveDynamicCollisions(*this, PairCandidates, mSettings.FixedTimeStep);
    ResolveStaticCollisions(*this, DynamicActors, StaticActors, mSettings.FixedTimeStep);
    ResolveKinematicCollisions(ActorRepository, mSettings.Gravity, mSettings.FixedTimeStep);
    IntegrateDynamicActors(*this, ActorRepository, mSettings.FixedTimeStep);
    ResolveStaticCollisions(*this, DynamicActors, StaticActors, mSettings.FixedTimeStep);
    ResolveKinematicCollisions(ActorRepository, mSettings.Gravity, mSettings.FixedTimeStep);
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

bool PhysicsWorld::ResolveKinematicTerrainContact(PhysicsActorBase& Actor) {
    bool HasResolved{ ResolveKinematicActorTerrainContactInternal(*mActorRepository, Actor) };
    return HasResolved;
}

void PhysicsWorld::ResolveKinematicTerrainContacts() {
    IPhysicsActorRepository& ActorRepository{ GetActorRepository() };
    std::vector<PhysicsKinematicActor*> KinematicActors{ ActorRepository.CollectKinematicActors() };
    std::size_t KinematicActorCount{ KinematicActors.size() };
    for (std::size_t ActorIndex{ 0U }; ActorIndex < KinematicActorCount; ++ActorIndex) {
        PhysicsKinematicActor* KinematicActor{ KinematicActors[ActorIndex] };
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
