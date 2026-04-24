#pragma once

/*
PhysicsLib Header Guide
Role:
- Defines the Actor alias that uses the Static collision solver and Static integrater.
Initialization:
- Fill PhysicsStaticActor::ActorDesc, construct directly, or register it through PhysicsWorld::AddActor.
Usage:
- Use it as the default type for immovable collision targets.
Notes:
- The default Static solver does not resolve collisions, so use specialized derived types such as Terrain when needed.
*/

#include "PhysicsLib/Actors/PhysicsActor.h"

using PhysicsStaticActor = PhysicsActor<StaticSolver, StaticIntegrater, DefaultConstraintSolver, PhysicsActorBase::PhysicsActorType::Static>;
