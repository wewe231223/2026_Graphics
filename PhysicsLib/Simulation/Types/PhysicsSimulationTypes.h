#pragma once

/*
PhysicsLib Header Guide
Role:
- Defines common types for simulation events and Dynamic collision pair candidates.
Initialization:
- These are value types; initialize required fields with brace initialization.
Usage:
- Used by PhysicsWorld::GetPublishedEvents and IPhysicsSpatialQuery::QueryDynamicCollisionPairs.
Notes:
- Actor pointers are non-owning and are valid only while the ActorRepository owns the Actors.
*/

#include <cstdint>

#include "PhysicsLib/Actors/PhysicsDynamicActor.h"

class PhysicsActorBase;

enum class PhysicsSimulationEventType : std::uint32_t {
    StaticCollisionResolved = 0U,
    DynamicCollisionResolved = 1U
};

struct PhysicsDynamicCollisionPairCandidate {
    PhysicsDynamicActor* mFirstActor;
    PhysicsDynamicActor* mSecondActor;
};

struct PhysicsSimulationEvent {
    PhysicsSimulationEventType mEventType;
    const PhysicsActorBase* mFirstActor;
    const PhysicsActorBase* mSecondActor;
};
