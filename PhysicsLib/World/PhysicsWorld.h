#pragma once

/*
PhysicsLib Header Guide
Role:
- Provides the main PhysicsLib entry point for Actor creation, fixed-step updates, interpolation, and collision events.
Initialization:
- Construct with WorldSettings containing FixedTimeStep and Gravity, or default construct and call Initialize.
Usage:
- Register Actors with CreateDynamicActor, CreateKinematicActor, CreateTerrainActor, or AddActor, then call Update or StepSimulation.
Notes:
- Update runs accumulated fixed steps, and TryGetInterpolatedActorTransform returns render interpolation state.
*/

#include <cstddef>
#include <memory>
#include <vector>

#include <DirectXTK12/SimpleMath.h>

#include "PhysicsLib/Common.h"

class PhysicsFrameAccumulator final {
public:
    struct ActorState {
        const PhysicsActorBase* mActorPointer;
        PhysicsActorBase::PhysicsActorType mActorType;
        DirectX::SimpleMath::Vector3 mPosition;
        DirectX::SimpleMath::Quaternion mOrientation;
        DirectX::SimpleMath::Vector3 mScale;
    };

public:
    PhysicsFrameAccumulator();
    ~PhysicsFrameAccumulator();
    PhysicsFrameAccumulator(const PhysicsFrameAccumulator& Other);
    PhysicsFrameAccumulator& operator=(const PhysicsFrameAccumulator& Other);
    PhysicsFrameAccumulator(PhysicsFrameAccumulator&& Other) noexcept;
    PhysicsFrameAccumulator& operator=(PhysicsFrameAccumulator&& Other) noexcept;

    explicit PhysicsFrameAccumulator(float FixedTimeStep);

public:
    void Initialize(float FixedTimeStep);
    void AddDeltaTime(float DeltaTime);
    bool TryConsumeFixedStep();
    float GetAccumulatedTime() const;
    float GetInterpolationAlpha() const;

    void SynchronizeStatePair(const IPhysicsActorRepository& ActorRepository);
    void CapturePreviousState(const IPhysicsActorRepository& ActorRepository);
    void CaptureCurrentState(const IPhysicsActorRepository& ActorRepository);
    bool TryGetInterpolatedState(const PhysicsActorBase& Actor, DirectX::SimpleMath::Vector3& OutPosition, DirectX::SimpleMath::Quaternion& OutOrientation, DirectX::SimpleMath::Vector3& OutScale) const;

private:
    void CaptureState(std::vector<ActorState>& OutStates, const IPhysicsActorRepository& ActorRepository) const;
    bool TryGetActorState(const std::vector<ActorState>& States, const PhysicsActorBase& Actor, ActorState& OutActorState) const;

private:
    float mFixedTimeStep;
    float mAccumulatedTime;
    std::vector<ActorState> mPreviousStates;
    std::vector<ActorState> mCurrentStates;
};

class PhysicsWorld final : public IPhysicsWorld {
public:
    using WorldSettings = IPhysicsWorld::WorldSettings;

public:
    PhysicsWorld();
    ~PhysicsWorld() override;
    PhysicsWorld(const PhysicsWorld& Other);
    PhysicsWorld& operator=(const PhysicsWorld& Other);
    PhysicsWorld(PhysicsWorld&& Other) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&& Other) noexcept;

    explicit PhysicsWorld(const WorldSettings& Settings);

public:
    void Initialize(const WorldSettings& Settings) override;

    PhysicsDynamicActor* CreateDynamicActor(const PhysicsDynamicActor::ActorDesc& Desc) override;
    PhysicsKinematicActor* CreateKinematicActor(const PhysicsKinematicActor::ActorDesc& Desc) override;
    PhysicsTerrainActor* CreateTerrainActor(const PhysicsTerrainActor::ActorDesc& Desc) override;
    void AddActor(std::unique_ptr<PhysicsActorBase> Actor) override;
    void ClearActors() override;

    PhysicsActorBase* GetActor(std::size_t Index) override;
    const PhysicsActorBase* GetActor(std::size_t Index) const override;
    PhysicsTerrainActor* GetTerrainActor(std::size_t Index) override;
    const PhysicsTerrainActor* GetTerrainActor(std::size_t Index) const override;
    std::size_t GetActorCount() const override;
    std::vector<PhysicsTerrainActor*> CollectTerrainActors() override;
    std::vector<const PhysicsTerrainActor*> CollectTerrainActors() const override;

    const WorldSettings& GetSettings() const override;
    float GetAccumulator() const override;
    float GetInterpolationAlpha() const override;
    std::size_t GetLastUpdateStepCount() const override;
    double GetLastUpdateStepElapsedMilliseconds() const override;
    double GetLastStepElapsedMilliseconds() const override;
    bool TryGetInterpolatedActorTransform(const PhysicsActorBase& Actor, DirectX::SimpleMath::Vector3& OutPosition, DirectX::SimpleMath::Quaternion& OutOrientation, DirectX::SimpleMath::Vector3& OutScale) const override;

    void StepSimulation() override;
    void Update(float DeltaTime) override;
    bool ResolveKinematicTerrainContact(PhysicsActorBase& Actor) override;
    void ResolveKinematicTerrainContacts() override;

    const DirectX::SimpleMath::Vector3& GetGravity() const override;
    IPhysicsActorRepository& GetActorRepository() override;
    const IPhysicsActorRepository& GetActorRepository() const override;
    IPhysicsSpatialQuery& GetSpatialQuery() override;
    const IPhysicsSpatialQuery& GetSpatialQuery() const override;
    void PublishEvent(PhysicsSimulationEventType EventType, const PhysicsActorBase* FirstActor, const PhysicsActorBase* SecondActor) override;
    void ClearPublishedEvents() override;
    const std::vector<PhysicsSimulationEvent>& GetPublishedEvents() const override;

private:
    void InitializeDependencies();

private:
    WorldSettings mSettings;
    PhysicsFrameAccumulator mFrameAccumulator;
    std::size_t mLastUpdateStepCount;
    double mLastUpdateStepElapsedMilliseconds;
    double mLastStepElapsedMilliseconds;
    std::unique_ptr<IPhysicsActorRepository> mActorRepository;
    std::unique_ptr<IPhysicsSpatialQuery> mSpatialQuery;
    std::vector<PhysicsSimulationEvent> mPublishedEvents;
};
