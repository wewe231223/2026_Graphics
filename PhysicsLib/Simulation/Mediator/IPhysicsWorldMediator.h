#pragma once

/*
PhysicsLib Header Guide
Role:
- Mediates access from Actor policies to PhysicsWorld gravity, repository, spatial query, and event publishing.
Initialization:
- Do not instantiate directly; PhysicsWorld provides the implementation.
Usage:
- Passed by reference to Integrater, CollisionSolver, and ConstraintSolver code that needs world state.
Notes:
- Repository and spatial query ownership stays in PhysicsWorld, so avoid long-lived cached references.
*/

#include <vector>

#include <DirectXTK12/SimpleMath.h>

#include "PhysicsLib/Simulation/Repository/IPhysicsActorRepository.h"
#include "PhysicsLib/Simulation/SpatialQuery/IPhysicsSpatialQuery.h"
#include "PhysicsLib/Simulation/Types/PhysicsSimulationTypes.h"

class IPhysicsWorldMediator {
public:
    IPhysicsWorldMediator();
    virtual ~IPhysicsWorldMediator();
    IPhysicsWorldMediator(const IPhysicsWorldMediator& Other);
    IPhysicsWorldMediator& operator=(const IPhysicsWorldMediator& Other);
    IPhysicsWorldMediator(IPhysicsWorldMediator&& Other) noexcept;
    IPhysicsWorldMediator& operator=(IPhysicsWorldMediator&& Other) noexcept;

public:
    virtual const DirectX::SimpleMath::Vector3& GetGravity() const = 0;

    virtual IPhysicsActorRepository& GetActorRepository() = 0;
    virtual const IPhysicsActorRepository& GetActorRepository() const = 0;

    virtual IPhysicsSpatialQuery& GetSpatialQuery() = 0;
    virtual const IPhysicsSpatialQuery& GetSpatialQuery() const = 0;

    virtual void PublishEvent(PhysicsSimulationEventType EventType, const PhysicsActorBase* FirstActor, const PhysicsActorBase* SecondActor) = 0;
    virtual void ClearPublishedEvents() = 0;
    virtual const std::vector<PhysicsSimulationEvent>& GetPublishedEvents() const = 0;
};
