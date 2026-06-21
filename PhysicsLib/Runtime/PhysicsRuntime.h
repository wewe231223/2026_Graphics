#pragma once

/*
PhysicsLib Header Guide
Role:
- Owns the asynchronous PhysicsWorld update loop and publishes triple-buffered simulation snapshots.
Initialization:
- Initialize with a PhysicsRuntimeScene template, RuntimeSettings, and world version.
Usage:
- Enqueue commands from the application thread, then copy an interpolated snapshot through CopyInterpolatedSnapshotForTime.
Notes:
- The runtime owns a copied scene template until the next Initialize call.
*/

#include <DirectXTK12/SimpleMath.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

#include "PhysicsLib/Runtime/PhysicsRuntimeTypes.h"
#include "PhysicsLib/Runtime/SpscRingQueue.h"
#include "PhysicsLib/World/PhysicsWorld.h"

class PhysicsRuntime final {
public:
    struct RuntimeSettings final {
        PhysicsWorld::WorldSettings mWorldSettings{};
        std::size_t mMaxSubSteps{ 4U };
    };

public:
    PhysicsRuntime();
    ~PhysicsRuntime();
    PhysicsRuntime(const PhysicsRuntime& Other) = delete;
    PhysicsRuntime& operator=(const PhysicsRuntime& Other) = delete;
    PhysicsRuntime(PhysicsRuntime&& Other) noexcept = delete;
    PhysicsRuntime& operator=(PhysicsRuntime&& Other) noexcept = delete;

public:
    bool Initialize(const PhysicsRuntimeScene& SceneTemplate, const RuntimeSettings& Settings, std::uint32_t InitialWorldVersion);
    void Shutdown();

    bool EnqueueCommand(const PhysicsCommand& Command);
    void PublishKinematicStates(const std::vector<PhysicsKinematicRuntimeState>& KinematicStates);

    bool CopyInterpolatedSnapshotForTime(double RenderPhysicsTime, PhysicsSnapshot& OutSnapshot) const;

    bool IsRunning() const;
    std::uint64_t LatestStepIndex() const;
    double LatestSimulationTimeSeconds() const;
    std::uint64_t PublishedSnapshotCount() const;

private:
    struct PhysicsSnapshotBuffer final {
        PhysicsSnapshot mSnapshot{};
        std::atomic<std::uint64_t> mVersion{};
        mutable std::atomic<std::uint32_t> mReaderCount{};
    };

    struct PhysicsSnapshotReadHandle final {
        const PhysicsSnapshot* mSnapshot{};
        std::uint32_t mBufferIndex{ std::numeric_limits<std::uint32_t>::max() };
        std::uint64_t mVersion{};
    };

    PhysicsCommand CreateResetSceneCommand(std::uint32_t WorldVersion) const;
    const PhysicsActorSnapshot* FindPhysicsActorSnapshot(const PhysicsSnapshot& Snapshot, ActorId ActorIdValue) const;
    PhysicsActorSnapshot InterpolatePhysicsActorSnapshot(const PhysicsActorSnapshot& PreviousActor, const PhysicsActorSnapshot& NextActor, float Alpha) const;
    void BuildInterpolatedPhysicsSnapshot(const PhysicsSnapshot& PreviousSnapshot, const PhysicsSnapshot& NextSnapshot, double RenderPhysicsTime, PhysicsSnapshot& OutSnapshot) const;

    std::uint64_t PackResetSceneCommand(const PhysicsCommand& Command) const;
    PhysicsCommand UnpackResetSceneCommand(std::uint64_t PackedCommand) const;

    bool TryConsumeCoalescedResetCommand(PhysicsCommand& OutCommand);
    bool TryAcquireSnapshotBuffer(const std::atomic<std::uint32_t>& PublishedSnapshotIndex, PhysicsSnapshotReadHandle& OutHandle) const;
    void ReleaseSnapshotBuffer(PhysicsSnapshotReadHandle& Handle) const;
    bool TrySelectWriteSnapshotBuffer(std::uint32_t& OutBufferIndex);
    void ResetSnapshotBuffers();
    void RunPhysicsThread();
    bool ProcessPendingCommandsAtFixedStepBoundary(double& OutTimeAccumulatorSeconds);
    bool ProcessCommand(const PhysicsCommand& Command, double& OutTimeAccumulatorSeconds);
    void ApplyResetSceneCommand(const PhysicsCommand& Command, double& OutTimeAccumulatorSeconds);
    void ApplyImpulseCommand(const PhysicsCommand& Command);
    void ApplySetKinematicVelocityCommand(const PhysicsCommand& Command);
    void ApplyAddForceCommand(const PhysicsCommand& Command);
    void ApplySetVelocityCommand(const PhysicsCommand& Command);
    void ApplySetKinematicTransformCommand(const PhysicsCommand& Command);
    void ApplySetLocalBoundingBoxCommand(const PhysicsCommand& Command);
    void ApplySetTerrainActorDescCommand(const PhysicsCommand& Command);
    void ApplySetActorActiveCommand(const PhysicsCommand& Command);
    void ApplyAddStaticActorCommand(const PhysicsCommand& Command);
    void ApplyPublishedKinematicStates();
    void BuildWorldFromScene();
    void PublishSnapshot(std::size_t LastUpdateStepCount, double LastUpdateStepElapsedMilliseconds, double LastStepElapsedMilliseconds);

private:
    static constexpr std::size_t SnapshotBufferCount{ 3U };
    static constexpr std::size_t CommandQueueCapacity{ 1024U };
    static constexpr std::uint32_t InvalidSnapshotBufferIndex{ std::numeric_limits<std::uint32_t>::max() };

private:
    RuntimeSettings mSettings;
    PhysicsRuntimeScene mSceneTemplate;
    PhysicsWorld mPhysicsWorld;
    std::uint32_t mCurrentWorldVersion;
    mutable std::array<PhysicsSnapshotBuffer, SnapshotBufferCount> mSnapshotBuffers;
    std::atomic<std::uint32_t> mPublishedSnapshotIndex;
    std::atomic<std::uint32_t> mPreviousSnapshotIndex;
    std::uint32_t mWriteSnapshotIndex;
    SpscRingQueue<PhysicsCommand, CommandQueueCapacity> mCommandQueue;
    std::atomic<std::uint64_t> mCoalescedResetCommand;
    std::atomic<bool> mHasCoalescedResetCommand;
    std::atomic<bool> mIsRunning;
    std::atomic<std::uint64_t> mLatestStepIndex;
    std::atomic<double> mLatestSimulationTimeSeconds;
    std::atomic<std::uint64_t> mPublishedSnapshotCount;
    std::thread mPhysicsThread;
    mutable std::mutex mKinematicStateMutex;
    std::vector<PhysicsKinematicRuntimeState> mPublishedKinematicStates;
    std::vector<PhysicsKinematicRuntimeState> mKinematicStateScratch;
};
