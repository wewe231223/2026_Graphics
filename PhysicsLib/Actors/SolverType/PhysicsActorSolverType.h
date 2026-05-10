#pragma once

/*
PhysicsLib Header Guide
Role:
- Collects solver and integrater aliases used by the default Actor types.
Initialization:
- No initialization is required; PhysicsDynamicActor, PhysicsKinematicActor, and PhysicsStaticActor use these aliases.
Usage:
- Reuse DynamicSolver, StaticSolver, KinematicSolver, and related aliases when defining custom Actor policies.
Notes:
- This header only provides aliases; the actual interfaces live in PhysicsSolverType.h.
*/

#include "PhysicsLib/Actors/CollisionSolver/PhysicsDynamicCollisionSolver.h"
#include "PhysicsLib/Actors/CollisionSolver/PhysicsKinematicCollisionSolver.h"
#include "PhysicsLib/Actors/CollisionSolver/PhysicsStaticCollisionSolver.h"
#include "PhysicsLib/Actors/ConstraintSolver/PhysicsNoConstraintSolver.h"
#include "PhysicsLib/Actors/Integrater/PhysicsDynamicIntegrater.h"
#include "PhysicsLib/Actors/Integrater/PhysicsKinematicIntegrater.h"
#include "PhysicsLib/Actors/Integrater/PhysicsStaticIntegrater.h"

using DynamicSolver = PhysicsDynamicCollisionSolver;
using StaticSolver = PhysicsStaticCollisionSolver;
using KinematicSolver = PhysicsKinematicCollisionSolver;
using DynamicIntegrater = PhysicsDynamicIntegrater;
using StaticIntegrater = PhysicsStaticIntegrater;
using KinematicIntegrater = PhysicsKinematicIntegrater;
using DefaultConstraintSolver = PhysicsNoConstraintSolver;
