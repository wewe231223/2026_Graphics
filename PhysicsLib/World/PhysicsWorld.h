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

#include "PhysicsLib/Actors/PhysicsActor.h"
#include "PhysicsLib/Actors/PhysicsDynamicActor.h"
#include "PhysicsLib/Actors/PhysicsKinematicActor.h"
#include "PhysicsLib/Actors/PhysicsTerrainActor.h"
#include "PhysicsLib/Simulation/Mediator/IPhysicsWorldMediator.h"
#include "PhysicsLib/Simulation/Types/PhysicsSimulationTypes.h"

class IPhysicsActorRepository;
class IPhysicsSpatialQuery;

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

class PhysicsWorld final : public IPhysicsWorldMediator {
public:
    struct WorldSettings {
        float FixedTimeStep;
        DirectX::SimpleMath::Vector3 Gravity;
    };

public:
    PhysicsWorld();
    ~PhysicsWorld() override;
    PhysicsWorld(const PhysicsWorld& Other);
    PhysicsWorld& operator=(const PhysicsWorld& Other);
    PhysicsWorld(PhysicsWorld&& Other) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&& Other) noexcept;

    explicit PhysicsWorld(const WorldSettings& Settings);

public:
    void Initialize(const WorldSettings& Settings);

    PhysicsDynamicActor* CreateDynamicActor(const PhysicsDynamicActor::ActorDesc& Desc);
    PhysicsKinematicActor* CreateKinematicActor(const PhysicsKinematicActor::ActorDesc& Desc);
    PhysicsTerrainActor* CreateTerrainActor(const PhysicsTerrainActor::ActorDesc& Desc);
    void AddActor(std::unique_ptr<PhysicsActorBase> Actor);
    void ClearActors();

    PhysicsActorBase* GetActor(std::size_t Index);
    const PhysicsActorBase* GetActor(std::size_t Index) const;
    std::size_t GetActorCount() const;

    const WorldSettings& GetSettings() const;
    float GetAccumulator() const;
    float GetInterpolationAlpha() const;
    std::size_t GetLastUpdateStepCount() const;
    double GetLastUpdateStepElapsedMilliseconds() const;
    double GetLastStepElapsedMilliseconds() const;
    bool TryGetInterpolatedActorTransform(const PhysicsActorBase& Actor, DirectX::SimpleMath::Vector3& OutPosition, DirectX::SimpleMath::Quaternion& OutOrientation, DirectX::SimpleMath::Vector3& OutScale) const;

    void StepSimulation();
    void Update(float DeltaTime);
    bool ResolveKinematicTerrainContact(PhysicsActorBase& Actor);
    void ResolveKinematicTerrainContacts();

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
