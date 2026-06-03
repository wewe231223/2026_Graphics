#pragma once

/*
PhysicsLib Header Guide
Role:
- Owns the asynchronous PhysicsWorld update loop and publishes triple-buffered simulation snapshots.
Initialization:
- Initialize with immutable PhysicsRuntimeScene templates, RuntimeSettings, initial scene index, and world version.
Usage:
- Enqueue commands from the application thread, then read snapshots through GetReadableSnapshotIndex and GetSnapshot.
Notes:
- The scene template pointer is non-owning and must remain valid until Shutdown completes.
*/

#include <DirectXTK12/SimpleMath.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
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
    bool Initialize(const std::vector<PhysicsRuntimeScene>* SceneTemplates, const RuntimeSettings& Settings, std::size_t InitialSceneIndex, std::uint32_t InitialWorldVersion);
    void Shutdown();

    bool EnqueueResetScene(std::size_t SceneIndex, std::uint32_t WorldVersion);
    bool EnqueueAddImpulse(ActorId ActorIdValue, const DirectX::SimpleMath::Vector3& Impulse);
    bool EnqueueSetKinematicVelocity(ActorId ActorIdValue, const DirectX::SimpleMath::Vector3& Velocity);
    bool EnqueueAddForce(ActorId ActorIdValue, const DirectX::SimpleMath::Vector3& Force);
    bool EnqueueSetVelocity(ActorId ActorIdValue, const DirectX::SimpleMath::Vector3& Velocity);
    bool EnqueueSetKinematicTransform(ActorId ActorIdValue, const DirectX::SimpleMath::Vector3& Position, const DirectX::SimpleMath::Quaternion& Orientation, const DirectX::SimpleMath::Vector3& Scale, bool IsTeleport, bool SetPosition, bool SetOrientation, bool SetScale);
    bool EnqueueSetLocalBoundingBox(ActorId ActorIdValue, const DirectX::BoundingOrientedBox& LocalBoundingBox);
    bool EnqueueSetTerrainActorDesc(ActorId ActorIdValue, const std::shared_ptr<const PhysicsTerrainActor::ActorDesc>& TerrainActorDesc);
    bool EnqueueSetActorActive(ActorId ActorIdValue, bool IsActive);
    void PublishKinematicStates(const std::vector<PhysicsKinematicRuntimeState>& KinematicStates);

    std::uint32_t GetReadableSnapshotIndex() const;
    const PhysicsSnapshot& GetSnapshot(std::uint32_t SnapshotIndex) const;
    bool TryGetSnapshotPairForTime(double RenderPhysicsTime, PhysicsSnapshot& OutPrevious, PhysicsSnapshot& OutNext, float& OutAlpha) const;

    bool IsRunning() const;
    std::uint64_t LatestStepIndex() const;
    double LatestSimulationTimeSeconds() const;
    std::uint64_t PublishedSnapshotCount() const;

private:
    static std::uint64_t PackResetSceneCommand(const PhysicsResetSceneCommand& Command);
    static PhysicsResetSceneCommand UnpackResetSceneCommand(std::uint64_t PackedCommand);

    bool TryConsumeCoalescedResetCommand(PhysicsResetSceneCommand& OutCommand);
    void RunPhysicsThread();
    bool ProcessPendingCommandsAtFixedStepBoundary(double& OutTimeAccumulatorSeconds);
    bool ProcessCommand(const PhysicsCommand& Command, double& OutTimeAccumulatorSeconds);
    void ApplyResetSceneCommand(const PhysicsResetSceneCommand& Command, double& OutTimeAccumulatorSeconds);
    void ApplyImpulseCommand(const PhysicsAddImpulseCommand& Command);
    void ApplySetKinematicVelocityCommand(const PhysicsSetKinematicVelocityCommand& Command);
    void ApplyAddForceCommand(const PhysicsAddForceCommand& Command);
    void ApplySetVelocityCommand(const PhysicsSetVelocityCommand& Command);
    void ApplySetKinematicTransformCommand(const PhysicsSetKinematicTransformCommand& Command);
    void ApplySetLocalBoundingBoxCommand(const PhysicsSetLocalBoundingBoxCommand& Command);
    void ApplySetTerrainActorDescCommand(const PhysicsSetTerrainActorDescCommand& Command);
    void ApplySetActorActiveCommand(const PhysicsSetActorActiveCommand& Command);
    void ApplyPublishedKinematicStates();
    void BuildWorldFromScene(std::size_t SceneIndex);
    void PublishSnapshot(std::size_t LastUpdateStepCount, double LastUpdateStepElapsedMilliseconds, double LastStepElapsedMilliseconds);

private:
    static constexpr std::size_t SnapshotBufferCount{ 3U };
    static constexpr std::size_t CommandQueueCapacity{ 1024U };

private:
    RuntimeSettings mSettings;
    const std::vector<PhysicsRuntimeScene>* mSceneTemplates;
    PhysicsWorld mPhysicsWorld;
    std::size_t mCurrentSceneIndex;
    std::uint32_t mCurrentWorldVersion;
    std::array<PhysicsSnapshot, SnapshotBufferCount> mSnapshotBuffers;
    mutable std::mutex mSnapshotMutex;
    std::atomic<std::uint32_t> mReadableSnapshotIndex;
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
