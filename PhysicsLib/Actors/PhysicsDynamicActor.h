#pragma once

/*
PhysicsLib Header Guide
Role:
- Defines the Actor alias that uses the Dynamic collision solver and Dynamic integrater.
Initialization:
- Fill PhysicsDynamicActor::ActorDesc and pass it to PhysicsWorld::CreateDynamicActor.
Usage:
- Represents moving Actors with mass; control movement through AddForce, AddImpulse, or SetVelocity.
Notes:
- This type is an alias, so it has no separate .cpp implementation and uses the PhysicsActor template implementation.
*/

#include "PhysicsLib/Actors/PhysicsActor.h"

using PhysicsDynamicActor = PhysicsActor<DynamicSolver, DynamicIntegrater, DefaultConstraintSolver, PhysicsActorBase::PhysicsActorType::Dynamic>;
