#include "PhysicsRuntime.h"

#include "PhysicsLib/Actors/PhysicsDynamicActor.h"
#include "PhysicsLib/Actors/PhysicsKinematicActor.h"
#include "PhysicsLib/Actors/PhysicsTerrainActor.h"

#include <DirectXTK12/SimpleMath.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>

PhysicsRuntime::PhysicsRuntime()
    : mSettings{},
      mSceneTemplates{},
      mPhysicsWorld{},
      mCurrentSceneIndex{},
      mCurrentWorldVersion{ 1U },
      mSnapshotBuffers{},
      mReadableSnapshotIndex{},
      mWriteSnapshotIndex{ 1U },
      mCommandQueue{},
      mCoalescedResetCommand{},
      mHasCoalescedResetCommand{},
      mIsRunning{},
      mPhysicsThread{} {
}

PhysicsRuntime::~PhysicsRuntime() {
    Shutdown();
}

bool PhysicsRuntime::Initialize(const std::vector<PhysicsRuntimeScene>* SceneTemplates, const RuntimeSettings& Settings, std::size_t InitialSceneIndex, std::uint32_t InitialWorldVersion) {
    if (mIsRunning.load(std::memory_order_acquire)) {
        return false;
    }

    if (SceneTemplates == nullptr || SceneTemplates->empty()) {
        return false;
    }

    mSceneTemplates = SceneTemplates;
    mSettings = Settings;
    if (mSettings.mWorldSettings.FixedTimeStep <= 0.0F) {
        mSettings.mWorldSettings.FixedTimeStep = 1.0F / 60.0F;
    }

    if (mSettings.mMaxSubSteps == 0U) {
        mSettings.mMaxSubSteps = 1U;
    }

    if (InitialSceneIndex >= mSceneTemplates->size()) {
        InitialSceneIndex = 0U;
    }

    mCurrentSceneIndex = InitialSceneIndex;
    mCurrentWorldVersion = InitialWorldVersion;

    std::size_t MaxActorCount{};
    std::size_t SceneCount{ mSceneTemplates->size() };
    for (std::size_t SceneIndex{ 0U }; SceneIndex < SceneCount; ++SceneIndex) {
        const PhysicsRuntimeScene& CurrentScene{ (*mSceneTemplates)[SceneIndex] };
        std::size_t CurrentActorCount{ CurrentScene.mActorSpawnInfos.size() };
        if (CurrentActorCount > MaxActorCount) {
            MaxActorCount = CurrentActorCount;
        }
    }

    for (std::size_t BufferIndex{ 0U }; BufferIndex < SnapshotBufferCount; ++BufferIndex) {
        PhysicsSnapshot& CurrentBuffer{ mSnapshotBuffers[BufferIndex] };
        CurrentBuffer.mWorldVersion = mCurrentWorldVersion;
        CurrentBuffer.mSceneIndex = mCurrentSceneIndex;
        CurrentBuffer.mActorCount = 0U;
        CurrentBuffer.mLastUpdateStepCount = 0U;
        CurrentBuffer.mLastUpdateStepElapsedMilliseconds = 0.0;
        CurrentBuffer.mLastStepElapsedMilliseconds = 0.0;
        CurrentBuffer.mActors.clear();
        CurrentBuffer.mActors.resize(MaxActorCount);
    }

    mReadableSnapshotIndex.store(0U, std::memory_order_release);
    mWriteSnapshotIndex = 1U;
    mCoalescedResetCommand.store(0U, std::memory_order_release);
    mHasCoalescedResetCommand.store(false, std::memory_order_release);

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

bool PhysicsRuntime::EnqueueResetScene(std::size_t SceneIndex, std::uint32_t WorldVersion) {
    PhysicsCommand NewCommand{};
    NewCommand.mType = PhysicsCommandType::ResetScene;
    NewCommand.mResetScene.mSceneIndex = SceneIndex;
    NewCommand.mResetScene.mWorldVersion = WorldVersion;

    bool Enqueued{ mCommandQueue.TryEnqueue(NewCommand) };
    if (Enqueued) {
        return true;
    }

    std::uint64_t PackedCommand{ PackResetSceneCommand(NewCommand.mResetScene) };
    mCoalescedResetCommand.store(PackedCommand, std::memory_order_release);
    mHasCoalescedResetCommand.store(true, std::memory_order_release);
    return true;
}

bool PhysicsRuntime::EnqueueAddImpulse(ActorId ActorIdValue, const DirectX::SimpleMath::Vector3& Impulse) {
    PhysicsCommand NewCommand{};
    NewCommand.mType = PhysicsCommandType::AddImpulse;
    NewCommand.mAddImpulse.mActorId = ActorIdValue;
    NewCommand.mAddImpulse.mImpulse = Impulse;

    bool Enqueued{ mCommandQueue.TryEnqueue(NewCommand) };
    return Enqueued;
}

bool PhysicsRuntime::EnqueueSetKinematicVelocity(ActorId ActorIdValue, const DirectX::SimpleMath::Vector3& Velocity) {
    PhysicsCommand NewCommand{};
    NewCommand.mType = PhysicsCommandType::SetKinematicVelocity;
    NewCommand.mSetKinematicVelocity.mActorId = ActorIdValue;
    NewCommand.mSetKinematicVelocity.mVelocity = Velocity;

    bool Enqueued{ mCommandQueue.TryEnqueue(NewCommand) };
    return Enqueued;
}

std::uint32_t PhysicsRuntime::GetReadableSnapshotIndex() const {
    std::uint32_t ReadableSnapshotIndex{ mReadableSnapshotIndex.load(std::memory_order_acquire) };
    return ReadableSnapshotIndex;
}

const PhysicsSnapshot& PhysicsRuntime::GetSnapshot(std::uint32_t SnapshotIndex) const {
    if (SnapshotIndex >= SnapshotBufferCount) {
        std::uint32_t ReadableSnapshotIndex{ mReadableSnapshotIndex.load(std::memory_order_acquire) };
        return mSnapshotBuffers[ReadableSnapshotIndex];
    }

    return mSnapshotBuffers[SnapshotIndex];
}

std::uint64_t PhysicsRuntime::PackResetSceneCommand(const PhysicsResetSceneCommand& Command) {
    std::uint64_t PackedSceneIndex{ static_cast<std::uint64_t>(Command.mSceneIndex) & 0xFFFFFFFFULL };
    std::uint64_t PackedVersion{ static_cast<std::uint64_t>(Command.mWorldVersion) & 0xFFFFFFFFULL };
    std::uint64_t PackedCommand{ (PackedSceneIndex << 32U) | PackedVersion };
    return PackedCommand;
}

PhysicsResetSceneCommand PhysicsRuntime::UnpackResetSceneCommand(std::uint64_t PackedCommand) {
    PhysicsResetSceneCommand UnpackedCommand{};
    UnpackedCommand.mSceneIndex = static_cast<std::size_t>((PackedCommand >> 32U) & 0xFFFFFFFFULL);
    UnpackedCommand.mWorldVersion = static_cast<std::uint32_t>(PackedCommand & 0xFFFFFFFFULL);
    return UnpackedCommand;
}

bool PhysicsRuntime::TryConsumeCoalescedResetCommand(PhysicsResetSceneCommand& OutCommand) {
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
    ApplyResetSceneCommand(PhysicsResetSceneCommand{ mCurrentSceneIndex, mCurrentWorldVersion }, TimeAccumulatorSeconds);

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

        bool ProcessedCommand{};
        PhysicsCommand CurrentCommand{};
        while (mCommandQueue.TryDequeue(CurrentCommand)) {
            ProcessCommand(CurrentCommand, TimeAccumulatorSeconds);
            ProcessedCommand = true;
        }

        PhysicsResetSceneCommand CoalescedResetCommand{};
        bool HasCoalescedResetCommand{ TryConsumeCoalescedResetCommand(CoalescedResetCommand) };
        if (HasCoalescedResetCommand) {
            ApplyResetSceneCommand(CoalescedResetCommand, TimeAccumulatorSeconds);
            ProcessedCommand = true;
        }

        double FixedStepSeconds{ static_cast<double>(mSettings.mWorldSettings.FixedTimeStep) };
        if (FixedStepSeconds <= 0.0) {
            FixedStepSeconds = 1.0 / 60.0;
        }

        std::size_t LastUpdateStepCount{};
        double LastUpdateStepElapsedMilliseconds{};
        double LastStepElapsedMilliseconds{};
        while (TimeAccumulatorSeconds >= FixedStepSeconds && LastUpdateStepCount < mSettings.mMaxSubSteps) {
            Clock::time_point StepStartTime{ Clock::now() };
            mPhysicsWorld.StepSimulation();
            Clock::time_point StepEndTime{ Clock::now() };

            std::chrono::duration<double, std::milli> StepElapsedDuration{ StepEndTime - StepStartTime };
            LastStepElapsedMilliseconds = StepElapsedDuration.count();
            LastUpdateStepElapsedMilliseconds += LastStepElapsedMilliseconds;
            TimeAccumulatorSeconds -= FixedStepSeconds;
            ++LastUpdateStepCount;
        }

        if (TimeAccumulatorSeconds >= FixedStepSeconds) {
            TimeAccumulatorSeconds = std::fmod(TimeAccumulatorSeconds, FixedStepSeconds);
        }

        if (LastUpdateStepCount > 0U) {
            PublishSnapshot(LastUpdateStepCount, LastUpdateStepElapsedMilliseconds, LastStepElapsedMilliseconds);
        }

        if (!ProcessedCommand && LastUpdateStepCount == 0U) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void PhysicsRuntime::ProcessCommand(const PhysicsCommand& Command, double& OutTimeAccumulatorSeconds) {
    if (Command.mType == PhysicsCommandType::ResetScene) {
        ApplyResetSceneCommand(Command.mResetScene, OutTimeAccumulatorSeconds);
        return;
    }

    if (Command.mType == PhysicsCommandType::AddImpulse) {
        ApplyImpulseCommand(Command.mAddImpulse);
        return;
    }

    if (Command.mType == PhysicsCommandType::SetKinematicVelocity) {
        ApplySetKinematicVelocityCommand(Command.mSetKinematicVelocity);
    }
}

void PhysicsRuntime::ApplyResetSceneCommand(const PhysicsResetSceneCommand& Command, double& OutTimeAccumulatorSeconds) {
    std::size_t NextSceneIndex{ Command.mSceneIndex };
    if (mSceneTemplates == nullptr || mSceneTemplates->empty()) {
        NextSceneIndex = 0U;
    } else if (NextSceneIndex >= mSceneTemplates->size()) {
        NextSceneIndex = 0U;
    }

    mCurrentSceneIndex = NextSceneIndex;
    mCurrentWorldVersion = Command.mWorldVersion;
    BuildWorldFromScene(mCurrentSceneIndex);
    OutTimeAccumulatorSeconds = 0.0;
    PublishSnapshot(0U, 0.0, 0.0);
}

void PhysicsRuntime::ApplyImpulseCommand(const PhysicsAddImpulseCommand& Command) {
    if (Command.mActorId == InvalidActorId) {
        return;
    }

    std::size_t ActorIndex{ static_cast<std::size_t>(Command.mActorId) };
    PhysicsActorBase* TargetActor{ mPhysicsWorld.GetActor(ActorIndex) };
    if (TargetActor == nullptr || TargetActor->GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic) {
        return;
    }

    PhysicsDynamicActor* DynamicActor{ static_cast<PhysicsDynamicActor*>(TargetActor) };
    DynamicActor->AddImpulse(Command.mImpulse);
}

void PhysicsRuntime::ApplySetKinematicVelocityCommand(const PhysicsSetKinematicVelocityCommand& Command) {
    if (Command.mActorId == InvalidActorId) {
        return;
    }

    std::size_t ActorIndex{ static_cast<std::size_t>(Command.mActorId) };
    PhysicsActorBase* TargetActor{ mPhysicsWorld.GetActor(ActorIndex) };
    if (TargetActor == nullptr || TargetActor->GetActorType() != PhysicsActorBase::PhysicsActorType::Kinematic) {
        return;
    }

    PhysicsKinematicActor* KinematicActor{ static_cast<PhysicsKinematicActor*>(TargetActor) };
    KinematicActor->SetVelocity(Command.mVelocity);
}

void PhysicsRuntime::BuildWorldFromScene(std::size_t SceneIndex) {
    mPhysicsWorld.Initialize(mSettings.mWorldSettings);

    if (mSceneTemplates == nullptr || mSceneTemplates->empty()) {
        return;
    }

    if (SceneIndex >= mSceneTemplates->size()) {
        return;
    }

    const PhysicsRuntimeScene& SelectedScene{ (*mSceneTemplates)[SceneIndex] };
    const std::vector<PhysicsActorSpawnInfo>& SpawnInfos{ SelectedScene.mActorSpawnInfos };
    std::size_t SpawnCount{ SpawnInfos.size() };
    for (std::size_t SpawnIndex{ 0U }; SpawnIndex < SpawnCount; ++SpawnIndex) {
        const PhysicsActorSpawnInfo& CurrentSpawnInfo{ SpawnInfos[SpawnIndex] };
        if (CurrentSpawnInfo.mActorType == PhysicsActorBase::PhysicsActorType::Static) {
            PhysicsTerrainActor* CreatedTerrainActor{ mPhysicsWorld.CreateTerrainActor(CurrentSpawnInfo.mTerrainActorDesc) };
            if (CreatedTerrainActor != nullptr) {
                CreatedTerrainActor->SetName(CurrentSpawnInfo.mName);
                CreatedTerrainActor->SetIsActive(CurrentSpawnInfo.mIsActive);
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
    PhysicsSnapshot& WriteBuffer{ mSnapshotBuffers[mWriteSnapshotIndex] };
    WriteBuffer.mWorldVersion = mCurrentWorldVersion;
    WriteBuffer.mSceneIndex = mCurrentSceneIndex;
    WriteBuffer.mLastUpdateStepCount = LastUpdateStepCount;
    WriteBuffer.mLastUpdateStepElapsedMilliseconds = LastUpdateStepElapsedMilliseconds;
    WriteBuffer.mLastStepElapsedMilliseconds = LastStepElapsedMilliseconds;

    std::size_t ActorCount{ mPhysicsWorld.GetActorCount() };
    if (ActorCount > WriteBuffer.mActors.size()) {
        ActorCount = WriteBuffer.mActors.size();
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

        if (CurrentActor->GetActorType() == PhysicsActorBase::PhysicsActorType::Dynamic) {
            const PhysicsDynamicActor* DynamicActor{ static_cast<const PhysicsDynamicActor*>(CurrentActor) };
            SnapshotActor.mPosition = DynamicActor->GetPosition();
            SnapshotActor.mOrientation = DynamicActor->GetOrientation();
            SnapshotActor.mScale = DynamicActor->GetScale();
            SnapshotActor.mWorldBoundingBox = DynamicActor->GetWorldBoundingBox();
            continue;
        }

        if (CurrentActor->GetActorType() == PhysicsActorBase::PhysicsActorType::Kinematic) {
            const PhysicsKinematicActor* KinematicActor{ static_cast<const PhysicsKinematicActor*>(CurrentActor) };
            SnapshotActor.mPosition = KinematicActor->GetPosition();
            SnapshotActor.mOrientation = KinematicActor->GetOrientation();
            SnapshotActor.mScale = KinematicActor->GetScale();
            SnapshotActor.mWorldBoundingBox = KinematicActor->GetWorldBoundingBox();
            continue;
        }

        const PhysicsTerrainActor* TerrainActor{ dynamic_cast<const PhysicsTerrainActor*>(CurrentActor) };
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

        SnapshotActor.mPosition = DirectX::SimpleMath::Vector3{};
        SnapshotActor.mOrientation = DirectX::SimpleMath::Quaternion{ 0.0F, 0.0F, 0.0F, 1.0F };
        SnapshotActor.mScale = DirectX::SimpleMath::Vector3{ 1.0F, 1.0F, 1.0F };
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


