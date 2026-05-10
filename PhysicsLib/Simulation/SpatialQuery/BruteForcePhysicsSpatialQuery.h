#pragma once

/*
PhysicsLib Header Guide
Role:
- Generates broad phase collision candidates by comparing Dynamic Actor fat bounding boxes.
Initialization:
- Default construct it for tests or let PhysicsWorld create it as an internal dependency.
Usage:
- Pass an ActorRepository to QueryDynamicCollisionPairs to get Dynamic Actor pair candidates.
Notes:
- Cost grows with Actor count, so large worlds should replace it with another IPhysicsSpatialQuery implementation.
*/

#include <memory>
#include <vector>

#include "IPhysicsSpatialQuery.h"

class BruteForcePhysicsSpatialQuery final : public IPhysicsSpatialQuery {
public:
    BruteForcePhysicsSpatialQuery();
    ~BruteForcePhysicsSpatialQuery() override;
    BruteForcePhysicsSpatialQuery(const BruteForcePhysicsSpatialQuery& Other);
    BruteForcePhysicsSpatialQuery& operator=(const BruteForcePhysicsSpatialQuery& Other);
    BruteForcePhysicsSpatialQuery(BruteForcePhysicsSpatialQuery&& Other) noexcept;
    BruteForcePhysicsSpatialQuery& operator=(BruteForcePhysicsSpatialQuery&& Other) noexcept;

public:
    std::unique_ptr<IPhysicsSpatialQuery> Clone() const override;
    std::vector<PhysicsDynamicCollisionPairCandidate> QueryDynamicCollisionPairs(IPhysicsActorRepository& ActorRepository) const override;
};
