#pragma once

/*
PhysicsLib Header Guide
Role:
- Defines the Actor alias that uses the Kinematic collision solver and Kinematic integrater.
Initialization:
- Fill PhysicsKinematicActor::ActorDesc and pass it to PhysicsWorld::CreateKinematicActor.
Usage:
- Use it for Actors whose velocity is controlled directly by input or scripted logic.
Notes:
- This type is an alias, so it has no separate .cpp implementation and uses the PhysicsActor template implementation.
*/

#include "PhysicsLib/Actors/PhysicsActor.h"

using PhysicsKinematicActor = PhysicsActor<KinematicSolver, KinematicIntegrater, DefaultConstraintSolver, PhysicsActorBase::PhysicsActorType::Kinematic>;
