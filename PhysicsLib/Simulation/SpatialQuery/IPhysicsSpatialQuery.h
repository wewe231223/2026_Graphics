#pragma once

/*
PhysicsLib Header Guide
Role:
- Abstracts broad phase collision candidate generation.
Initialization:
- Do not instantiate directly; use BruteForcePhysicsSpatialQuery or a custom implementation.
Usage:
- PhysicsWorld calls QueryDynamicCollisionPairs during a step to find Dynamic Actor collision candidates.
Notes:
- Implement Clone so PhysicsWorld copies can duplicate the spatial query.
*/

#include <memory>
#include <vector>

#include "PhysicsLib/Simulation/Repository/IPhysicsActorRepository.h"
#include "PhysicsLib/Simulation/Types/PhysicsSimulationTypes.h"

class IPhysicsSpatialQuery {
public:
    IPhysicsSpatialQuery();
    virtual ~IPhysicsSpatialQuery();
    IPhysicsSpatialQuery(const IPhysicsSpatialQuery& Other);
    IPhysicsSpatialQuery& operator=(const IPhysicsSpatialQuery& Other);
    IPhysicsSpatialQuery(IPhysicsSpatialQuery&& Other) noexcept;
    IPhysicsSpatialQuery& operator=(IPhysicsSpatialQuery&& Other) noexcept;

public:
    virtual std::unique_ptr<IPhysicsSpatialQuery> Clone() const = 0;
    virtual std::vector<PhysicsDynamicCollisionPairCandidate> QueryDynamicCollisionPairs(IPhysicsActorRepository& ActorRepository) const = 0;
};


