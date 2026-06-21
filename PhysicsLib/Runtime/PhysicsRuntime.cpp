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
    constexpr std::uint32_t SnapshotWriterReservationBit{ 1U << 31U };
}

PhysicsCommand PhysicsRuntime::CreateResetSceneCommand(std::uint32_t WorldVersion) const {
        PhysicsCommand Command{};
        Command.mType = PhysicsCommandType::ResetScene;
        Command.mWorldVersion = WorldVersion;
        return Command;
    }

const PhysicsActorSnapshot* PhysicsRuntime::FindPhysicsActorSnapshot(const PhysicsSnapshot& Snapshot, ActorId ActorIdValue) const {
        const std::vector<PhysicsActorSnapshot>::const_iterator SnapshotActorIterator{ std::lower_bound(Snapshot.mActors.begin(), Snapshot.mActors.end(), ActorIdValue, [](const PhysicsActorSnapshot& SnapshotActor, ActorId TargetActorId) {
            return SnapshotActor.mActorId < TargetActorId;
        }) };
        if (SnapshotActorIterator == Snapshot.mActors.end() || SnapshotActorIterator->mActorId != ActorIdValue) {
            return nullptr;
        }

        return &*SnapshotActorIterator;
    }

PhysicsActorSnapshot PhysicsRuntime::InterpolatePhysicsActorSnapshot(const PhysicsActorSnapshot& PreviousActor, const PhysicsActorSnapshot& NextActor, float Alpha) const {
        if (PreviousActor.mActorId != NextActor.mActorId || PreviousActor.mActorType != NextActor.mActorType) {
            return PreviousActor;
        }

        const float ClampedAlpha{ std::clamp(Alpha, 0.0F, 1.0F) };
        PhysicsActorSnapshot InterpolatedActor{ PreviousActor };
        InterpolatedActor.mIsActive = PreviousActor.mIsActive && NextActor.mIsActive;
        InterpolatedActor.mPosition = DirectX::SimpleMath::Vector3::Lerp(PreviousActor.mPosition, NextActor.mPosition, ClampedAlpha);
        InterpolatedActor.mOrientation = DirectX::SimpleMath::Quaternion::Slerp(PreviousActor.mOrientation, NextActor.mOrientation, ClampedAlpha);
        InterpolatedActor.mOrientation.Normalize();
        InterpolatedActor.mScale = DirectX::SimpleMath::Vector3::Lerp(PreviousActor.mScale, NextActor.mScale, ClampedAlpha);
        InterpolatedActor.mVelocity = DirectX::SimpleMath::Vector3::Lerp(PreviousActor.mVelocity, NextActor.mVelocity, ClampedAlpha);

        const DirectX::SimpleMath::Vector3 PreviousCenter{ PreviousActor.mWorldBoundingBox.Center };
        const DirectX::SimpleMath::Vector3 NextCenter{ NextActor.mWorldBoundingBox.Center };
        const DirectX::SimpleMath::Vector3 PreviousExtents{ PreviousActor.mWorldBoundingBox.Extents };
        const DirectX::SimpleMath::Vector3 NextExtents{ NextActor.mWorldBoundingBox.Extents };
        const DirectX::SimpleMath::Quaternion PreviousOrientation{ PreviousActor.mWorldBoundingBox.Orientation };
        const DirectX::SimpleMath::Quaternion NextOrientation{ NextActor.mWorldBoundingBox.Orientation };
        const DirectX::SimpleMath::Vector3 InterpolatedCenter{ DirectX::SimpleMath::Vector3::Lerp(PreviousCenter, NextCenter, ClampedAlpha) };
        const DirectX::SimpleMath::Vector3 InterpolatedExtents{ DirectX::SimpleMath::Vector3::Lerp(PreviousExtents, NextExtents, ClampedAlpha) };
        DirectX::SimpleMath::Quaternion InterpolatedBoundingBoxOrientation{ DirectX::SimpleMath::Quaternion::Slerp(PreviousOrientation, NextOrientation, ClampedAlpha) };
        InterpolatedBoundingBoxOrientation.Normalize();
        InterpolatedActor.mWorldBoundingBox.Center = DirectX::XMFLOAT3{ InterpolatedCenter.x, InterpolatedCenter.y, InterpolatedCenter.z };
        InterpolatedActor.mWorldBoundingBox.Extents = DirectX::XMFLOAT3{ InterpolatedExtents.x, InterpolatedExtents.y, InterpolatedExtents.z };
        InterpolatedActor.mWorldBoundingBox.Orientation = DirectX::XMFLOAT4{ InterpolatedBoundingBoxOrientation.x, InterpolatedBoundingBoxOrientation.y, InterpolatedBoundingBoxOrientation.z, InterpolatedBoundingBoxOrientation.w };
        return InterpolatedActor;
    }

void PhysicsRuntime::BuildInterpolatedPhysicsSnapshot(const PhysicsSnapshot& PreviousSnapshot, const PhysicsSnapshot& NextSnapshot, double RenderPhysicsTime, PhysicsSnapshot& OutSnapshot) const {
        const double TimeRange{ NextSnapshot.mSimulationTimeSeconds - PreviousSnapshot.mSimulationTimeSeconds };
        const double RawAlpha{ TimeRange > 0.0 ? (RenderPhysicsTime - PreviousSnapshot.mSimulationTimeSeconds) / TimeRange : 0.0 };
        const float Alpha{ static_cast<float>(std::clamp(RawAlpha, 0.0, 1.0)) };

        OutSnapshot.mWorldVersion = PreviousSnapshot.mWorldVersion;
        OutSnapshot.mStepIndex = NextSnapshot.mStepIndex;
        OutSnapshot.mSimulationTimeSeconds = PreviousSnapshot.mSimulationTimeSeconds + (TimeRange * static_cast<double>(Alpha));
        OutSnapshot.mPublishIndex = NextSnapshot.mPublishIndex;
        OutSnapshot.mActorCount = PreviousSnapshot.mActorCount;
        OutSnapshot.mTotalActorCount = NextSnapshot.mTotalActorCount;
        OutSnapshot.mLastUpdateStepCount = NextSnapshot.mLastUpdateStepCount;
        OutSnapshot.mLastUpdateStepElapsedMilliseconds = NextSnapshot.mLastUpdateStepElapsedMilliseconds;
        OutSnapshot.mLastStepElapsedMilliseconds = NextSnapshot.mLastStepElapsedMilliseconds;
        OutSnapshot.mActors.clear();
        OutSnapshot.mActors.reserve(PreviousSnapshot.mActors.size());

        for (const PhysicsActorSnapshot& PreviousActor : PreviousSnapshot.mActors) {
            const PhysicsActorSnapshot* NextActor{ FindPhysicsActorSnapshot(NextSnapshot, PreviousActor.mActorId) };
            if (NextActor == nullptr) {
                OutSnapshot.mActors.push_back(PreviousActor);
                continue;
            }

            OutSnapshot.mActors.push_back(InterpolatePhysicsActorSnapshot(PreviousActor, *NextActor, Alpha));
        }
}

PhysicsRuntime::PhysicsRuntime()
    : mSettings{},
      mSceneTemplate{},
      mPhysicsWorld{},
      mCurrentWorldVersion{ 1U },
      mSnapshotBuffers{},
      mPublishedSnapshotIndex{ InvalidSnapshotBufferIndex },
      mPreviousSnapshotIndex{ InvalidSnapshotBufferIndex },
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

    ResetSnapshotBuffers();

    mLatestStepIndex.store(0U, std::memory_order_release);
    mLatestSimulationTimeSeconds.store(0.0, std::memory_order_release);
    mPublishedSnapshotCount.store(0U, std::memory_order_release);
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

bool PhysicsRuntime::CopyInterpolatedSnapshotForTime(double RenderPhysicsTime, PhysicsSnapshot& OutSnapshot) const {
    constexpr std::size_t MaximumAttemptCount{ 3U };
    for (std::size_t AttemptIndex{}; AttemptIndex < MaximumAttemptCount; ++AttemptIndex) {
        PhysicsSnapshotReadHandle LatestHandle{};
        if (TryAcquireSnapshotBuffer(mPublishedSnapshotIndex, LatestHandle) == false) {
            return false;
        }

        PhysicsSnapshotReadHandle PreviousHandle{};
        const bool HasPreviousSnapshot{ TryAcquireSnapshotBuffer(mPreviousSnapshotIndex, PreviousHandle) };
        const std::uint32_t LatestSnapshotIndex{ mPublishedSnapshotIndex.load(std::memory_order_acquire) };
        const std::uint32_t PreviousSnapshotIndex{ mPreviousSnapshotIndex.load(std::memory_order_acquire) };
        const bool IsCurrentPair{ LatestHandle.mBufferIndex == LatestSnapshotIndex && (HasPreviousSnapshot == false || PreviousHandle.mBufferIndex == PreviousSnapshotIndex) };
        if (IsCurrentPair == true) {
            if (HasPreviousSnapshot == false) {
                OutSnapshot = *LatestHandle.mSnapshot;
                ReleaseSnapshotBuffer(LatestHandle);
                return OutSnapshot.mPublishIndex != 0U;
            }

            BuildInterpolatedPhysicsSnapshot(*PreviousHandle.mSnapshot, *LatestHandle.mSnapshot, RenderPhysicsTime, OutSnapshot);
            ReleaseSnapshotBuffer(PreviousHandle);
            ReleaseSnapshotBuffer(LatestHandle);
            return OutSnapshot.mPublishIndex != 0U;
        }

        if (HasPreviousSnapshot == true) {
            ReleaseSnapshotBuffer(PreviousHandle);
        }

        ReleaseSnapshotBuffer(LatestHandle);
    }

    return false;
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

std::uint64_t PhysicsRuntime::PackResetSceneCommand(const PhysicsCommand& Command) const {
    std::uint64_t PackedVersion{ static_cast<std::uint64_t>(Command.mWorldVersion) & 0xFFFFFFFFULL };
    std::uint64_t PackedCommand{ PackedVersion };
    return PackedCommand;
}

PhysicsCommand PhysicsRuntime::UnpackResetSceneCommand(std::uint64_t PackedCommand) const {
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

bool PhysicsRuntime::TryAcquireSnapshotBuffer(const std::atomic<std::uint32_t>& PublishedSnapshotIndex, PhysicsSnapshotReadHandle& OutHandle) const {
    while (true) {
        const std::uint32_t BufferIndex{ PublishedSnapshotIndex.load(std::memory_order_acquire) };
        if (BufferIndex == InvalidSnapshotBufferIndex || BufferIndex >= SnapshotBufferCount) {
            return false;
        }

        const PhysicsSnapshotBuffer& Buffer{ mSnapshotBuffers[BufferIndex] };
        std::uint32_t ReaderCount{ Buffer.mReaderCount.load(std::memory_order_acquire) };
        while ((ReaderCount & SnapshotWriterReservationBit) == 0U) {
            if (Buffer.mReaderCount.compare_exchange_weak(ReaderCount, ReaderCount + 1U, std::memory_order_acq_rel, std::memory_order_acquire) == false) {
                continue;
            }

            const std::uint64_t Version{ Buffer.mVersion.load(std::memory_order_acquire) };
            if (PublishedSnapshotIndex.load(std::memory_order_acquire) == BufferIndex && Version != 0U) {
                OutHandle.mSnapshot = &Buffer.mSnapshot;
                OutHandle.mBufferIndex = BufferIndex;
                OutHandle.mVersion = Version;
                return true;
            }

            Buffer.mReaderCount.fetch_sub(1U, std::memory_order_release);
            break;
        }
    }
}

void PhysicsRuntime::ReleaseSnapshotBuffer(PhysicsSnapshotReadHandle& Handle) const {
    if (Handle.mBufferIndex < SnapshotBufferCount) {
        PhysicsSnapshotBuffer& Buffer{ mSnapshotBuffers[Handle.mBufferIndex] };
        Buffer.mReaderCount.fetch_sub(1U, std::memory_order_release);
    }

    Handle = PhysicsSnapshotReadHandle{};
}

bool PhysicsRuntime::TrySelectWriteSnapshotBuffer(std::uint32_t& OutBufferIndex) {
    const std::uint32_t PublishedSnapshotIndex{ mPublishedSnapshotIndex.load(std::memory_order_acquire) };
    const std::uint32_t PreviousSnapshotIndex{ mPreviousSnapshotIndex.load(std::memory_order_acquire) };
    for (std::size_t Offset{}; Offset < SnapshotBufferCount; ++Offset) {
        const std::uint32_t BufferIndex{ static_cast<std::uint32_t>((static_cast<std::size_t>(mWriteSnapshotIndex) + Offset) % SnapshotBufferCount) };
        if (BufferIndex == PublishedSnapshotIndex || BufferIndex == PreviousSnapshotIndex) {
            continue;
        }

        PhysicsSnapshotBuffer& Buffer{ mSnapshotBuffers[BufferIndex] };
        std::uint32_t ExpectedReaderCount{};
        if (Buffer.mReaderCount.compare_exchange_strong(ExpectedReaderCount, SnapshotWriterReservationBit, std::memory_order_acq_rel, std::memory_order_acquire) == false) {
            continue;
        }

        OutBufferIndex = BufferIndex;
        return true;
    }

    return false;
}

void PhysicsRuntime::ResetSnapshotBuffers() {
    mPublishedSnapshotIndex.store(InvalidSnapshotBufferIndex, std::memory_order_release);
    mPreviousSnapshotIndex.store(InvalidSnapshotBufferIndex, std::memory_order_release);

    for (PhysicsSnapshotBuffer& Buffer : mSnapshotBuffers) {
        while (Buffer.mReaderCount.load(std::memory_order_acquire) != 0U) {
            std::this_thread::yield();
        }

        Buffer.mReaderCount.store(SnapshotWriterReservationBit, std::memory_order_release);
        PhysicsSnapshot& Snapshot{ Buffer.mSnapshot };
        Snapshot.mWorldVersion = mCurrentWorldVersion;
        Snapshot.mStepIndex = 0U;
        Snapshot.mSimulationTimeSeconds = 0.0;
        Snapshot.mPublishIndex = 0U;
        Snapshot.mActorCount = 0U;
        Snapshot.mTotalActorCount = 0U;
        Snapshot.mLastUpdateStepCount = 0U;
        Snapshot.mLastUpdateStepElapsedMilliseconds = 0.0;
        Snapshot.mLastStepElapsedMilliseconds = 0.0;
        Snapshot.mActors.clear();
        Buffer.mVersion.store(0U, std::memory_order_release);
        Buffer.mReaderCount.store(0U, std::memory_order_release);
    }

    mWriteSnapshotIndex = 0U;
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

    ResetSnapshotBuffers();

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
    std::uint32_t WriteBufferIndex{};
    if (TrySelectWriteSnapshotBuffer(WriteBufferIndex) == false) {
        return;
    }

    PhysicsSnapshotBuffer& SnapshotBuffer{ mSnapshotBuffers[WriteBufferIndex] };
    PhysicsSnapshot& WriteBuffer{ SnapshotBuffer.mSnapshot };
    WriteBuffer.mWorldVersion = mCurrentWorldVersion;
    WriteBuffer.mStepIndex = mLatestStepIndex.load(std::memory_order_acquire);
    WriteBuffer.mSimulationTimeSeconds = mLatestSimulationTimeSeconds.load(std::memory_order_acquire);
    WriteBuffer.mPublishIndex = mPublishedSnapshotCount.fetch_add(1U, std::memory_order_acq_rel) + 1U;
    WriteBuffer.mLastUpdateStepCount = LastUpdateStepCount;
    WriteBuffer.mLastUpdateStepElapsedMilliseconds = LastUpdateStepElapsedMilliseconds;
    WriteBuffer.mLastStepElapsedMilliseconds = LastStepElapsedMilliseconds;

    const std::size_t ActorCount{ mPhysicsWorld.GetActorCount() };
    WriteBuffer.mTotalActorCount = ActorCount;
    WriteBuffer.mActors.clear();
    for (std::size_t ActorIndex{ 0U }; ActorIndex < ActorCount; ++ActorIndex) {
        const PhysicsActorBase* CurrentActor{ mPhysicsWorld.GetActor(ActorIndex) };
        if (CurrentActor == nullptr || CurrentActor->GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic) {
            continue;
        }

        PhysicsActorSnapshot SnapshotActor{};
        SnapshotActor.mActorId = static_cast<ActorId>(ActorIndex);
        SnapshotActor.mActorType = PhysicsActorBase::PhysicsActorType::Dynamic;
        SnapshotActor.mIsActive = CurrentActor->GetIsActive();
        const PhysicsDynamicActor* DynamicActor{ static_cast<const PhysicsDynamicActor*>(CurrentActor) };
        SnapshotActor.mPosition = DynamicActor->GetPosition();
        SnapshotActor.mOrientation = DynamicActor->GetOrientation();
        SnapshotActor.mScale = DynamicActor->GetScale();
        SnapshotActor.mVelocity = DynamicActor->GetVelocity();
        SnapshotActor.mWorldBoundingBox = DynamicActor->GetWorldBoundingBox();
        WriteBuffer.mActors.push_back(SnapshotActor);
    }

    WriteBuffer.mActorCount = WriteBuffer.mActors.size();
    SnapshotBuffer.mVersion.store(WriteBuffer.mPublishIndex, std::memory_order_release);

    const std::uint32_t PreviousSnapshotIndex{ mPublishedSnapshotIndex.load(std::memory_order_acquire) };
    mPreviousSnapshotIndex.store(PreviousSnapshotIndex, std::memory_order_release);
    mPublishedSnapshotIndex.store(WriteBufferIndex, std::memory_order_release);
    SnapshotBuffer.mReaderCount.store(0U, std::memory_order_release);
    mWriteSnapshotIndex = static_cast<std::uint32_t>((static_cast<std::size_t>(WriteBufferIndex) + 1U) % SnapshotBufferCount);
}


