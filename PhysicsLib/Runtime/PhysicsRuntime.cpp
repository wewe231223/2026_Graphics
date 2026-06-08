#include "PhysicsRuntime.h"

#include "PhysicsLib/Actors/PhysicsDynamicActor.h"
#include "PhysicsLib/Actors/PhysicsKinematicActor.h"
#include "PhysicsLib/Actors/PhysicsStaticActor.h"
#include "PhysicsLib/Actors/PhysicsTerrainActor.h"

#include <DirectXTK12/SimpleMath.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace {
    PhysicsCommand CreateResetSceneCommand(std::uint32_t WorldVersion) {
        PhysicsCommand Command{};
        Command.mType = PhysicsCommandType::ResetScene;
        Command.mWorldVersion = WorldVersion;
        return Command;
    }
}

PhysicsRuntime::PhysicsRuntime()
    : mSettings{},
      mSceneTemplate{},
      mPhysicsWorld{},
      mCurrentWorldVersion{ 1U },
      mSnapshotBuffers{},
      mSnapshotMutex{},
      mReadableSnapshotIndex{},
      mWriteSnapshotIndex{ 1U },
      mCommandQueue{},
      mCoalescedResetCommand{},
      mHasCoalescedResetCommand{},
      mIsRunning{},
      mLatestStepIndex{},
      mLatestSimulationTimeSeconds{},
      mPublishedSnapshotCount{},
      mPhysicsThread{},
      mKinematicStateMutex{},
      mPublishedKinematicStates{},
      mKinematicStateScratch{} {
}

PhysicsRuntime::~PhysicsRuntime() {
    Shutdown();
}

bool PhysicsRuntime::Initialize(const PhysicsRuntimeScene& SceneTemplate, const RuntimeSettings& Settings, std::uint32_t InitialWorldVersion) {
    if (mIsRunning.load(std::memory_order_acquire)) {
        return false;
    }

    mSceneTemplate = SceneTemplate;
    mSettings = Settings;
    if (mSettings.mWorldSettings.FixedTimeStep <= 0.0F) {
        mSettings.mWorldSettings.FixedTimeStep = 1.0F / 60.0F;
    }

    if (mSettings.mMaxSubSteps == 0U) {
        mSettings.mMaxSubSteps = 1U;
    }

    mCurrentWorldVersion = InitialWorldVersion;

    std::size_t MaxActorCount{ mSceneTemplate.mActorSpawnInfos.size() };

    {
        std::lock_guard<std::mutex> SnapshotLock{ mSnapshotMutex };
        for (std::size_t BufferIndex{ 0U }; BufferIndex < SnapshotBufferCount; ++BufferIndex) {
            PhysicsSnapshot& CurrentBuffer{ mSnapshotBuffers[BufferIndex] };
            CurrentBuffer.mWorldVersion = mCurrentWorldVersion;
            CurrentBuffer.mStepIndex = 0U;
            CurrentBuffer.mSimulationTimeSeconds = 0.0;
            CurrentBuffer.mPublishIndex = 0U;
            CurrentBuffer.mActorCount = 0U;
            CurrentBuffer.mLastUpdateStepCount = 0U;
            CurrentBuffer.mLastUpdateStepElapsedMilliseconds = 0.0;
            CurrentBuffer.mLastStepElapsedMilliseconds = 0.0;
            CurrentBuffer.mActors.clear();
            CurrentBuffer.mActors.resize(MaxActorCount);
        }
    }

    mLatestStepIndex.store(0U, std::memory_order_release);
    mLatestSimulationTimeSeconds.store(0.0, std::memory_order_release);
    mPublishedSnapshotCount.store(0U, std::memory_order_release);
    mReadableSnapshotIndex.store(0U, std::memory_order_release);
    mWriteSnapshotIndex = 1U;
    mCoalescedResetCommand.store(0U, std::memory_order_release);
    mHasCoalescedResetCommand.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> KinematicStateLock{ mKinematicStateMutex };
        mPublishedKinematicStates.clear();
    }

    mKinematicStateScratch.clear();

    mIsRunning.store(true, std::memory_order_release);
    mPhysicsThread = std::thread{ &PhysicsRuntime::RunPhysicsThread, this };

    return true;
}

void PhysicsRuntime::Shutdown() {
    if (!mIsRunning.load(std::memory_order_acquire)) {
        return;
    }

    mIsRunning.store(false, std::memory_order_release);
    if (mPhysicsThread.joinable()) {
        mPhysicsThread.join();
    }
}

bool PhysicsRuntime::EnqueueCommand(const PhysicsCommand& Command) {
    if (Command.mType == PhysicsCommandType::SetTerrainActorDesc && Command.mTerrainActorDesc == nullptr) {
        return false;
    }

    bool Enqueued{ mCommandQueue.TryEnqueue(Command) };
    if (Enqueued == true) {
        return true;
    }

    if (Command.mType != PhysicsCommandType::ResetScene) {
        return false;
    }

    std::uint64_t PackedCommand{ PackResetSceneCommand(Command) };
    mCoalescedResetCommand.store(PackedCommand, std::memory_order_release);
    mHasCoalescedResetCommand.store(true, std::memory_order_release);
    return true;
}

void PhysicsRuntime::PublishKinematicStates(const std::vector<PhysicsKinematicRuntimeState>& KinematicStates) {
    std::lock_guard<std::mutex> KinematicStateLock{ mKinematicStateMutex };
    mPublishedKinematicStates = KinematicStates;
}

std::uint32_t PhysicsRuntime::GetReadableSnapshotIndex() const {
    std::uint32_t ReadableSnapshotIndex{ mReadableSnapshotIndex.load(std::memory_order_acquire) };
    return ReadableSnapshotIndex;
}

const PhysicsSnapshot& PhysicsRuntime::GetSnapshot(std::uint32_t SnapshotIndex) const {
    static thread_local PhysicsSnapshot SnapshotCopy{};
    std::lock_guard<std::mutex> SnapshotLock{ mSnapshotMutex };
    if (SnapshotIndex >= SnapshotBufferCount) {
        std::uint32_t ReadableSnapshotIndex{ mReadableSnapshotIndex.load(std::memory_order_acquire) };
        SnapshotCopy = mSnapshotBuffers[ReadableSnapshotIndex];
        return SnapshotCopy;
    }

    SnapshotCopy = mSnapshotBuffers[SnapshotIndex];
    return SnapshotCopy;
}

bool PhysicsRuntime::TryGetSnapshotPairForTime(double RenderPhysicsTime, PhysicsSnapshot& OutPrevious, PhysicsSnapshot& OutNext, float& OutAlpha) const {
    std::array<const PhysicsSnapshot*, SnapshotBufferCount> CandidateSnapshots{};
    std::size_t CandidateCount{};

    {
        std::lock_guard<std::mutex> SnapshotLock{ mSnapshotMutex };
        std::uint32_t ReadableSnapshotIndex{ mReadableSnapshotIndex.load(std::memory_order_acquire) };
        const PhysicsSnapshot& LatestSnapshot{ mSnapshotBuffers[ReadableSnapshotIndex] };
        if (LatestSnapshot.mPublishIndex == 0U) {
            return false;
        }

        std::uint32_t LatestWorldVersion{ LatestSnapshot.mWorldVersion };
        for (std::size_t BufferIndex{ 0U }; BufferIndex < SnapshotBufferCount; ++BufferIndex) {
            const PhysicsSnapshot& CandidateSnapshot{ mSnapshotBuffers[BufferIndex] };
            if (CandidateSnapshot.mPublishIndex == 0U || CandidateSnapshot.mWorldVersion != LatestWorldVersion) {
                continue;
            }

            CandidateSnapshots[CandidateCount] = &CandidateSnapshot;
            ++CandidateCount;
        }

        if (CandidateCount == 0U) {
            return false;
        }

        std::sort(CandidateSnapshots.begin(), CandidateSnapshots.begin() + CandidateCount, [](const PhysicsSnapshot* Left, const PhysicsSnapshot* Right) {
            if (Left->mSimulationTimeSeconds == Right->mSimulationTimeSeconds) {
                return Left->mPublishIndex < Right->mPublishIndex;
            }

            return Left->mSimulationTimeSeconds < Right->mSimulationTimeSeconds;
        });

        if (CandidateCount == 1U) {
            OutPrevious = *CandidateSnapshots[0U];
            OutNext = *CandidateSnapshots[0U];
            OutAlpha = 0.0F;
            return true;
        }

        const PhysicsSnapshot* PreviousSnapshot{ CandidateSnapshots[0U] };
        const PhysicsSnapshot* NextSnapshot{ CandidateSnapshots[1U] };
        if (RenderPhysicsTime <= CandidateSnapshots[0U]->mSimulationTimeSeconds) {
            PreviousSnapshot = CandidateSnapshots[0U];
            NextSnapshot = CandidateSnapshots[1U];
        } else if (RenderPhysicsTime >= CandidateSnapshots[CandidateCount - 1U]->mSimulationTimeSeconds) {
            PreviousSnapshot = CandidateSnapshots[CandidateCount - 2U];
            NextSnapshot = CandidateSnapshots[CandidateCount - 1U];
        } else {
            for (std::size_t CandidateIndex{ 0U }; CandidateIndex + 1U < CandidateCount; ++CandidateIndex) {
                const PhysicsSnapshot* CurrentPrevious{ CandidateSnapshots[CandidateIndex] };
                const PhysicsSnapshot* CurrentNext{ CandidateSnapshots[CandidateIndex + 1U] };
                if (RenderPhysicsTime < CurrentPrevious->mSimulationTimeSeconds || RenderPhysicsTime > CurrentNext->mSimulationTimeSeconds) {
                    continue;
                }

                PreviousSnapshot = CurrentPrevious;
                NextSnapshot = CurrentNext;
                break;
            }
        }

        OutPrevious = *PreviousSnapshot;
        OutNext = *NextSnapshot;
    }

    double TimeRange{ OutNext.mSimulationTimeSeconds - OutPrevious.mSimulationTimeSeconds };
    if (TimeRange <= 0.0) {
        OutAlpha = 0.0F;
        return true;
    }

    double Alpha{ (RenderPhysicsTime - OutPrevious.mSimulationTimeSeconds) / TimeRange };
    Alpha = std::clamp(Alpha, 0.0, 1.0);
    OutAlpha = static_cast<float>(Alpha);
    return true;
}

bool PhysicsRuntime::IsRunning() const {
    bool IsRuntimeRunning{ mIsRunning.load(std::memory_order_acquire) };
    return IsRuntimeRunning;
}

std::uint64_t PhysicsRuntime::LatestStepIndex() const {
    std::uint64_t StepIndex{ mLatestStepIndex.load(std::memory_order_acquire) };
    return StepIndex;
}

double PhysicsRuntime::LatestSimulationTimeSeconds() const {
    double SimulationTimeSeconds{ mLatestSimulationTimeSeconds.load(std::memory_order_acquire) };
    return SimulationTimeSeconds;
}

std::uint64_t PhysicsRuntime::PublishedSnapshotCount() const {
    std::uint64_t SnapshotCount{ mPublishedSnapshotCount.load(std::memory_order_acquire) };
    return SnapshotCount;
}

std::uint64_t PhysicsRuntime::PackResetSceneCommand(const PhysicsCommand& Command) {
    std::uint64_t PackedVersion{ static_cast<std::uint64_t>(Command.mWorldVersion) & 0xFFFFFFFFULL };
    std::uint64_t PackedCommand{ PackedVersion };
    return PackedCommand;
}

PhysicsCommand PhysicsRuntime::UnpackResetSceneCommand(std::uint64_t PackedCommand) {
    PhysicsCommand UnpackedCommand{};
    UnpackedCommand.mType = PhysicsCommandType::ResetScene;
    UnpackedCommand.mWorldVersion = static_cast<std::uint32_t>(PackedCommand & 0xFFFFFFFFULL);
    return UnpackedCommand;
}

bool PhysicsRuntime::TryConsumeCoalescedResetCommand(PhysicsCommand& OutCommand) {
    bool HasCommand{ mHasCoalescedResetCommand.exchange(false, std::memory_order_acq_rel) };
    if (!HasCommand) {
        return false;
    }

    std::uint64_t PackedCommand{ mCoalescedResetCommand.load(std::memory_order_acquire) };
    OutCommand = UnpackResetSceneCommand(PackedCommand);
    return true;
}

void PhysicsRuntime::RunPhysicsThread() {
    double TimeAccumulatorSeconds{};
    ApplyResetSceneCommand(CreateResetSceneCommand(mCurrentWorldVersion), TimeAccumulatorSeconds);

    using Clock = std::chrono::steady_clock;
    Clock::time_point PreviousTickTime{ Clock::now() };
    while (mIsRunning.load(std::memory_order_acquire)) {
        Clock::time_point CurrentTickTime{ Clock::now() };
        std::chrono::duration<double> TickElapsedDuration{ CurrentTickTime - PreviousTickTime };
        PreviousTickTime = CurrentTickTime;
        double TickElapsedSeconds{ TickElapsedDuration.count() };
        if (TickElapsedSeconds < 0.0) {
            TickElapsedSeconds = 0.0;
        }

        TimeAccumulatorSeconds += TickElapsedSeconds;

        double FixedStepSeconds{ static_cast<double>(mSettings.mWorldSettings.FixedTimeStep) };
        if (FixedStepSeconds <= 0.0) {
            FixedStepSeconds = 1.0 / 60.0;
        }

        bool ProcessedCommand{};
        std::size_t LastUpdateStepCount{};
        while (TimeAccumulatorSeconds >= FixedStepSeconds && LastUpdateStepCount < mSettings.mMaxSubSteps) {
            bool ResetApplied{ ProcessPendingCommandsAtFixedStepBoundary(TimeAccumulatorSeconds) };
            ProcessedCommand = ProcessedCommand || ResetApplied;
            if (ResetApplied) {
                break;
            }

            Clock::time_point StepStartTime{ Clock::now() };
            ApplyPublishedKinematicStates();
            mPhysicsWorld.TickKinematicActors(mSettings.mWorldSettings.FixedTimeStep);
            mPhysicsWorld.StepSimulation();
            Clock::time_point StepEndTime{ Clock::now() };

            std::chrono::duration<double, std::milli> StepElapsedDuration{ StepEndTime - StepStartTime };
            double LastStepElapsedMilliseconds{ StepElapsedDuration.count() };
            TimeAccumulatorSeconds -= FixedStepSeconds;
            ++LastUpdateStepCount;
            std::uint64_t NextStepIndex{ mLatestStepIndex.load(std::memory_order_relaxed) + 1U };
            double NextSimulationTimeSeconds{ mLatestSimulationTimeSeconds.load(std::memory_order_relaxed) + FixedStepSeconds };
            mLatestStepIndex.store(NextStepIndex, std::memory_order_release);
            mLatestSimulationTimeSeconds.store(NextSimulationTimeSeconds, std::memory_order_release);
            PublishSnapshot(1U, LastStepElapsedMilliseconds, LastStepElapsedMilliseconds);
        }

        if (TimeAccumulatorSeconds >= FixedStepSeconds) {
            TimeAccumulatorSeconds = std::fmod(TimeAccumulatorSeconds, FixedStepSeconds);
        }

        if (!ProcessedCommand && LastUpdateStepCount == 0U) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

bool PhysicsRuntime::ProcessPendingCommandsAtFixedStepBoundary(double& OutTimeAccumulatorSeconds) {
    bool ResetApplied{};
    PhysicsCommand CurrentCommand{};
    while (mCommandQueue.TryDequeue(CurrentCommand)) {
        bool CurrentResetApplied{ ProcessCommand(CurrentCommand, OutTimeAccumulatorSeconds) };
        ResetApplied = ResetApplied || CurrentResetApplied;
    }

    PhysicsCommand CoalescedResetCommand{};
    bool HasCoalescedResetCommand{ TryConsumeCoalescedResetCommand(CoalescedResetCommand) };
    if (HasCoalescedResetCommand) {
        ApplyResetSceneCommand(CoalescedResetCommand, OutTimeAccumulatorSeconds);
        ResetApplied = true;
    }

    return ResetApplied;
}

bool PhysicsRuntime::ProcessCommand(const PhysicsCommand& Command, double& OutTimeAccumulatorSeconds) {
    switch (Command.mType) {
        case PhysicsCommandType::ResetScene:
            ApplyResetSceneCommand(Command, OutTimeAccumulatorSeconds);
            return true;

        case PhysicsCommandType::AddImpulse:
            ApplyImpulseCommand(Command);
            return false;

        case PhysicsCommandType::SetKinematicVelocity:
            ApplySetKinematicVelocityCommand(Command);
            return false;

        case PhysicsCommandType::AddForce:
            ApplyAddForceCommand(Command);
            return false;

        case PhysicsCommandType::SetVelocity:
            ApplySetVelocityCommand(Command);
            return false;

        case PhysicsCommandType::SetKinematicTransform:
            ApplySetKinematicTransformCommand(Command);
            return false;

        case PhysicsCommandType::SetLocalBoundingBox:
            ApplySetLocalBoundingBoxCommand(Command);
            return false;

        case PhysicsCommandType::SetTerrainActorDesc:
            ApplySetTerrainActorDescCommand(Command);
            return false;

        case PhysicsCommandType::SetActorActive:
            ApplySetActorActiveCommand(Command);
            return false;

        case PhysicsCommandType::AddStaticActor:
            ApplyAddStaticActorCommand(Command);
            return false;

        default:
            return false;
    }
}

void PhysicsRuntime::ApplyResetSceneCommand(const PhysicsCommand& Command, double& OutTimeAccumulatorSeconds) {
    mCurrentWorldVersion = Command.mWorldVersion;
    BuildWorldFromScene();
    OutTimeAccumulatorSeconds = 0.0;
    mLatestStepIndex.store(0U, std::memory_order_release);
    mLatestSimulationTimeSeconds.store(0.0, std::memory_order_release);
    {
        std::lock_guard<std::mutex> KinematicStateLock{ mKinematicStateMutex };
        mPublishedKinematicStates.clear();
    }

    mKinematicStateScratch.clear();

    {
        std::lock_guard<std::mutex> SnapshotLock{ mSnapshotMutex };
        for (std::size_t BufferIndex{ 0U }; BufferIndex < SnapshotBufferCount; ++BufferIndex) {
            PhysicsSnapshot& CurrentBuffer{ mSnapshotBuffers[BufferIndex] };
            CurrentBuffer.mWorldVersion = mCurrentWorldVersion;
            CurrentBuffer.mStepIndex = 0U;
            CurrentBuffer.mSimulationTimeSeconds = 0.0;
            CurrentBuffer.mPublishIndex = 0U;
            CurrentBuffer.mActorCount = 0U;
            CurrentBuffer.mLastUpdateStepCount = 0U;
            CurrentBuffer.mLastUpdateStepElapsedMilliseconds = 0.0;
            CurrentBuffer.mLastStepElapsedMilliseconds = 0.0;
        }

        mReadableSnapshotIndex.store(0U, std::memory_order_release);
        mWriteSnapshotIndex = 0U;
    }

    PublishSnapshot(0U, 0.0, 0.0);
}

void PhysicsRuntime::ApplyImpulseCommand(const PhysicsCommand& Command) {
    if (Command.mActorId == InvalidActorId) {
        return;
    }

    std::size_t ActorIndex{ static_cast<std::size_t>(Command.mActorId) };
    PhysicsActorBase* TargetActor{ mPhysicsWorld.GetActor(ActorIndex) };
    if (TargetActor == nullptr) {
        return;
    }

    if (TargetActor->GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic) {
        return;
    }

    TargetActor->AddImpulse(Command.mVector);
}

void PhysicsRuntime::ApplySetKinematicVelocityCommand(const PhysicsCommand& Command) {
    if (Command.mActorId == InvalidActorId) {
        return;
    }

    std::size_t ActorIndex{ static_cast<std::size_t>(Command.mActorId) };
    PhysicsActorBase* TargetActor{ mPhysicsWorld.GetActor(ActorIndex) };
    if (TargetActor == nullptr || TargetActor->GetActorType() != PhysicsActorBase::PhysicsActorType::Kinematic) {
        return;
    }

    PhysicsKinematicActor* KinematicActor{ static_cast<PhysicsKinematicActor*>(TargetActor) };
    KinematicActor->SetVelocity(Command.mVector);
}

void PhysicsRuntime::ApplyAddForceCommand(const PhysicsCommand& Command) {
    if (Command.mActorId == InvalidActorId) {
        return;
    }

    std::size_t ActorIndex{ static_cast<std::size_t>(Command.mActorId) };
    PhysicsActorBase* TargetActor{ mPhysicsWorld.GetActor(ActorIndex) };
    if (TargetActor == nullptr) {
        return;
    }

    if (TargetActor->GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic) {
        return;
    }

    TargetActor->AddForce(Command.mVector);
}

void PhysicsRuntime::ApplySetVelocityCommand(const PhysicsCommand& Command) {
    if (Command.mActorId == InvalidActorId) {
        return;
    }

    std::size_t ActorIndex{ static_cast<std::size_t>(Command.mActorId) };
    PhysicsActorBase* TargetActor{ mPhysicsWorld.GetActor(ActorIndex) };
    if (TargetActor == nullptr) {
        return;
    }

    if (TargetActor->GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic) {
        return;
    }

    TargetActor->SetVelocity(Command.mVector);
}

void PhysicsRuntime::ApplySetKinematicTransformCommand(const PhysicsCommand& Command) {
    if (Command.mActorId == InvalidActorId) {
        return;
    }

    std::size_t ActorIndex{ static_cast<std::size_t>(Command.mActorId) };
    PhysicsActorBase* TargetActor{ mPhysicsWorld.GetActor(ActorIndex) };
    if (TargetActor == nullptr || TargetActor->GetActorType() != PhysicsActorBase::PhysicsActorType::Kinematic) {
        return;
    }

    PhysicsKinematicActor* KinematicActor{ static_cast<PhysicsKinematicActor*>(TargetActor) };
    if (Command.mSetScale == true) {
        KinematicActor->SetScale(Command.mScale);
    }

    if (Command.mSetOrientation == true) {
        KinematicActor->SetOrientation(Command.mOrientation);
    }

    if (Command.mSetPosition == true) {
        KinematicActor->SetPosition(Command.mPosition);
    }

    if (Command.mIsTeleport == true) {
        mPhysicsWorld.MarkKinematicActorTeleported(*KinematicActor);
    }
}

void PhysicsRuntime::ApplySetLocalBoundingBoxCommand(const PhysicsCommand& Command) {
    if (Command.mActorId == InvalidActorId) {
        return;
    }

    std::size_t ActorIndex{ static_cast<std::size_t>(Command.mActorId) };
    PhysicsActorBase* TargetActor{ mPhysicsWorld.GetActor(ActorIndex) };
    if (TargetActor == nullptr) {
        return;
    }

    TargetActor->SetLocalBoundingBox(Command.mLocalBoundingBox);
}

void PhysicsRuntime::ApplySetTerrainActorDescCommand(const PhysicsCommand& Command) {
    if (Command.mActorId == InvalidActorId || Command.mTerrainActorDesc == nullptr) {
        return;
    }

    std::size_t ActorIndex{ static_cast<std::size_t>(Command.mActorId) };
    PhysicsTerrainActor* TerrainActor{ mPhysicsWorld.GetTerrainActor(ActorIndex) };
    if (TerrainActor == nullptr) {
        return;
    }

    TerrainActor->SetActorDesc(*Command.mTerrainActorDesc);
}

void PhysicsRuntime::ApplySetActorActiveCommand(const PhysicsCommand& Command) {
    if (Command.mActorId == InvalidActorId) {
        return;
    }

    std::size_t ActorIndex{ static_cast<std::size_t>(Command.mActorId) };
    PhysicsActorBase* TargetActor{ mPhysicsWorld.GetActor(ActorIndex) };
    if (TargetActor == nullptr) {
        return;
    }

    TargetActor->SetIsActive(Command.mIsActive);
}

void PhysicsRuntime::ApplyAddStaticActorCommand(const PhysicsCommand& Command) {
    if (Command.mActorId == InvalidActorId) {
        return;
    }

    const std::size_t ActorIndex{ static_cast<std::size_t>(Command.mActorId) };
    const std::size_t ActorCount{ mPhysicsWorld.GetActorCount() };
    PhysicsStaticActor::ActorDesc ActorDesc{ Command.mStaticActorDesc };
    ActorDesc.ActorType = PhysicsActorBase::PhysicsActorType::Static;
    ActorDesc.Mass = 0.0F;

    if (ActorIndex < ActorCount) {
        PhysicsActorBase* ExistingActor{ mPhysicsWorld.GetActor(ActorIndex) };
        if (ExistingActor == nullptr || ExistingActor->GetActorType() != PhysicsActorBase::PhysicsActorType::Static) {
            return;
        }

        ExistingActor->SetName(ActorDesc.Name);
        ExistingActor->SetIsActive(ActorDesc.IsActive);
        ExistingActor->SetFlags(ActorDesc.Flags);
        ExistingActor->SetFriction(ActorDesc.Friction);
        ExistingActor->SetRestitution(ActorDesc.Restitution);
        ExistingActor->SetLocalBoundingBox(ActorDesc.LocalBoundingBox);
        ExistingActor->SetScale(ActorDesc.Scale);
        ExistingActor->SetRotation(ActorDesc.Rotation);
        ExistingActor->SetPosition(ActorDesc.Position);
        return;
    }

    if (ActorIndex != ActorCount) {
        return;
    }

    std::unique_ptr<PhysicsActorBase> NewActor{ std::make_unique<PhysicsStaticActor>(ActorDesc) };
    mPhysicsWorld.AddActor(std::move(NewActor));
}

void PhysicsRuntime::ApplyPublishedKinematicStates() {
    {
        std::lock_guard<std::mutex> KinematicStateLock{ mKinematicStateMutex };
        mKinematicStateScratch = mPublishedKinematicStates;
    }

    const std::size_t KinematicStateCount{ mKinematicStateScratch.size() };
    for (std::size_t StateIndex{ 0U }; StateIndex < KinematicStateCount; ++StateIndex) {
        const PhysicsKinematicRuntimeState& KinematicState{ mKinematicStateScratch[StateIndex] };
        if (KinematicState.mActorId == InvalidActorId) {
            continue;
        }

        PhysicsActorBase* TargetActor{ mPhysicsWorld.GetActor(static_cast<std::size_t>(KinematicState.mActorId)) };
        if (TargetActor == nullptr || TargetActor->GetActorType() != PhysicsActorBase::PhysicsActorType::Kinematic) {
            continue;
        }

        PhysicsKinematicActor* KinematicActor{ static_cast<PhysicsKinematicActor*>(TargetActor) };
        KinematicActor->SetIsActive(KinematicState.mIsActive);
        KinematicActor->SetScale(KinematicState.mScale);
        KinematicActor->SetOrientation(KinematicState.mOrientation);
        KinematicActor->SetPosition(KinematicState.mPosition);
        KinematicActor->SetVelocity(KinematicState.mVelocity);
    }
}

void PhysicsRuntime::BuildWorldFromScene() {
    mPhysicsWorld.Initialize(mSettings.mWorldSettings);

    const PhysicsRuntimeScene& SelectedScene{ mSceneTemplate };
    const std::vector<PhysicsActorSpawnInfo>& SpawnInfos{ SelectedScene.mActorSpawnInfos };
    std::size_t SpawnCount{ SpawnInfos.size() };
    for (std::size_t SpawnIndex{ 0U }; SpawnIndex < SpawnCount; ++SpawnIndex) {
        const PhysicsActorSpawnInfo& CurrentSpawnInfo{ SpawnInfos[SpawnIndex] };
        if (CurrentSpawnInfo.mIsTerrainActor == true) {
            PhysicsTerrainActor* CreatedTerrainActor{ mPhysicsWorld.CreateTerrainActor(CurrentSpawnInfo.mTerrainActorDesc) };
            if (CreatedTerrainActor != nullptr) {
                CreatedTerrainActor->SetName(CurrentSpawnInfo.mName);
                CreatedTerrainActor->SetIsActive(CurrentSpawnInfo.mIsActive);
            }
            continue;
        }

        if (CurrentSpawnInfo.mActorType == PhysicsActorBase::PhysicsActorType::Static) {
            std::unique_ptr<PhysicsActorBase> NewActor{ std::make_unique<PhysicsStaticActor>(CurrentSpawnInfo.mDynamicActorDesc) };
            PhysicsActorBase* CreatedStaticActor{ NewActor.get() };
            mPhysicsWorld.AddActor(std::move(NewActor));
            if (CreatedStaticActor != nullptr) {
                CreatedStaticActor->SetName(CurrentSpawnInfo.mName);
                CreatedStaticActor->SetIsActive(CurrentSpawnInfo.mIsActive);
            }
            continue;
        }

        if (CurrentSpawnInfo.mActorType == PhysicsActorBase::PhysicsActorType::Kinematic) {
            PhysicsKinematicActor* CreatedKinematicActor{ mPhysicsWorld.CreateKinematicActor(CurrentSpawnInfo.mDynamicActorDesc) };
            if (CreatedKinematicActor != nullptr) {
                CreatedKinematicActor->SetName(CurrentSpawnInfo.mName);
                CreatedKinematicActor->SetIsActive(CurrentSpawnInfo.mIsActive);
            }

            continue;
        }

        PhysicsDynamicActor* CreatedDynamicActor{ mPhysicsWorld.CreateDynamicActor(CurrentSpawnInfo.mDynamicActorDesc) };
        if (CreatedDynamicActor == nullptr) {
            continue;
        }

        if (CurrentSpawnInfo.mHasInitialImpulse) {
            CreatedDynamicActor->AddImpulse(CurrentSpawnInfo.mInitialImpulse);
        }
    }
}

void PhysicsRuntime::PublishSnapshot(std::size_t LastUpdateStepCount, double LastUpdateStepElapsedMilliseconds, double LastStepElapsedMilliseconds) {
    std::lock_guard<std::mutex> SnapshotLock{ mSnapshotMutex };
    PhysicsSnapshot& WriteBuffer{ mSnapshotBuffers[mWriteSnapshotIndex] };
    WriteBuffer.mWorldVersion = mCurrentWorldVersion;
    WriteBuffer.mStepIndex = mLatestStepIndex.load(std::memory_order_acquire);
    WriteBuffer.mSimulationTimeSeconds = mLatestSimulationTimeSeconds.load(std::memory_order_acquire);
    WriteBuffer.mPublishIndex = mPublishedSnapshotCount.fetch_add(1U, std::memory_order_acq_rel) + 1U;
    WriteBuffer.mLastUpdateStepCount = LastUpdateStepCount;
    WriteBuffer.mLastUpdateStepElapsedMilliseconds = LastUpdateStepElapsedMilliseconds;
    WriteBuffer.mLastStepElapsedMilliseconds = LastStepElapsedMilliseconds;

    const std::size_t ActorCount{ mPhysicsWorld.GetActorCount() };
    if (ActorCount > WriteBuffer.mActors.size()) {
        for (PhysicsSnapshot& SnapshotBuffer : mSnapshotBuffers) {
            SnapshotBuffer.mActors.resize(ActorCount);
        }
    }

    WriteBuffer.mActorCount = ActorCount;
    for (std::size_t ActorIndex{ 0U }; ActorIndex < ActorCount; ++ActorIndex) {
        const PhysicsActorBase* CurrentActor{ mPhysicsWorld.GetActor(ActorIndex) };
        PhysicsActorSnapshot& SnapshotActor{ WriteBuffer.mActors[ActorIndex] };
        if (CurrentActor == nullptr) {
            SnapshotActor = PhysicsActorSnapshot{};
            continue;
        }

        SnapshotActor.mActorId = static_cast<ActorId>(ActorIndex);
        SnapshotActor.mActorType = CurrentActor->GetActorType();
        SnapshotActor.mIsActive = CurrentActor->GetIsActive();
        SnapshotActor.mVelocity = DirectX::SimpleMath::Vector3{};

        if (CurrentActor->GetActorType() == PhysicsActorBase::PhysicsActorType::Dynamic) {
            const PhysicsDynamicActor* DynamicActor{ static_cast<const PhysicsDynamicActor*>(CurrentActor) };
            SnapshotActor.mPosition = DynamicActor->GetPosition();
            SnapshotActor.mOrientation = DynamicActor->GetOrientation();
            SnapshotActor.mScale = DynamicActor->GetScale();
            SnapshotActor.mVelocity = DynamicActor->GetVelocity();
            SnapshotActor.mWorldBoundingBox = DynamicActor->GetWorldBoundingBox();
            continue;
        }

        if (CurrentActor->GetActorType() == PhysicsActorBase::PhysicsActorType::Kinematic) {
            const PhysicsKinematicActor* KinematicActor{ static_cast<const PhysicsKinematicActor*>(CurrentActor) };
            SnapshotActor.mPosition = KinematicActor->GetPosition();
            SnapshotActor.mOrientation = KinematicActor->GetOrientation();
            SnapshotActor.mScale = KinematicActor->GetScale();
            SnapshotActor.mVelocity = KinematicActor->GetVelocity();
            SnapshotActor.mWorldBoundingBox = KinematicActor->GetWorldBoundingBox();
            continue;
        }

        const PhysicsTerrainActor* TerrainActor{ mPhysicsWorld.GetTerrainActor(ActorIndex) };
        if (TerrainActor != nullptr) {
            PhysicsTerrainActor::ActorDesc TerrainActorDesc{ TerrainActor->GetActorDesc() };
            SnapshotActor.mPosition = TerrainActorDesc.Position;
            SnapshotActor.mOrientation = TerrainActor->GetOrientation();
            SnapshotActor.mScale = TerrainActorDesc.Scale;

            DirectX::BoundingOrientedBox TerrainWorldBoundingBox{};
            TerrainWorldBoundingBox.Center = DirectX::XMFLOAT3{ TerrainActorDesc.Position.x, TerrainActorDesc.Position.y, TerrainActorDesc.Position.z };
            float WorldExtentX{ std::abs(TerrainActorDesc.HalfExtentX * TerrainActorDesc.Scale.x) };
            float WorldExtentZ{ std::abs(TerrainActorDesc.HalfExtentZ * TerrainActorDesc.Scale.z) };
            float WorldExtentY{ std::max(std::abs(TerrainActorDesc.HeightFieldMaxHeight * TerrainActorDesc.Scale.y), 0.5F) };
            TerrainWorldBoundingBox.Extents = DirectX::XMFLOAT3{ WorldExtentX, WorldExtentY, WorldExtentZ };
            TerrainWorldBoundingBox.Orientation = DirectX::XMFLOAT4{ SnapshotActor.mOrientation.x, SnapshotActor.mOrientation.y, SnapshotActor.mOrientation.z, SnapshotActor.mOrientation.w };
            SnapshotActor.mWorldBoundingBox = TerrainWorldBoundingBox;
            continue;
        }

        if (CurrentActor->GetActorType() == PhysicsActorBase::PhysicsActorType::Static) {
            SnapshotActor.mPosition = CurrentActor->GetPosition();
            SnapshotActor.mOrientation = CurrentActor->GetOrientation();
            SnapshotActor.mScale = CurrentActor->GetScale();
            SnapshotActor.mWorldBoundingBox = CurrentActor->GetWorldBoundingBox();
            continue;
        }

        SnapshotActor.mPosition = DirectX::SimpleMath::Vector3{};
        SnapshotActor.mOrientation = DirectX::SimpleMath::Quaternion{ 0.0F, 0.0F, 0.0F, 1.0F };
        SnapshotActor.mScale = DirectX::SimpleMath::Vector3{ 1.0F, 1.0F, 1.0F };
        SnapshotActor.mVelocity = DirectX::SimpleMath::Vector3{};
        SnapshotActor.mWorldBoundingBox = DirectX::BoundingOrientedBox{};
    }

    mReadableSnapshotIndex.store(mWriteSnapshotIndex, std::memory_order_release);

    std::uint32_t NextWriteIndex{ (mWriteSnapshotIndex + 1U) % static_cast<std::uint32_t>(SnapshotBufferCount) };
    std::uint32_t ReadableSnapshotIndex{ mReadableSnapshotIndex.load(std::memory_order_acquire) };
    if (NextWriteIndex == ReadableSnapshotIndex) {
        NextWriteIndex = (NextWriteIndex + 1U) % static_cast<std::uint32_t>(SnapshotBufferCount);
    }

    mWriteSnapshotIndex = NextWriteIndex;
}


