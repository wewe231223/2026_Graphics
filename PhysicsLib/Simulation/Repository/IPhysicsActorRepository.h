#pragma once

/*
PhysicsLib Header Guide
Role:
- Abstracts creation, lookup, and typed collection of Actors owned by PhysicsWorld.
Initialization:
- Do not instantiate directly; use PhysicsActorRepository or a custom repository implementation.
Usage:
- Create Actors with CreateDynamicActor, CreateKinematicActor, or CreateTerrainActor and query them with GetActor or Collect functions.
Notes:
- Returned Actor pointers are owned by the repository and become invalid after ClearActors.
*/

#include <cstddef>
#include <memory>
#include <vector>

#include "PhysicsLib/Actors/PhysicsActor.h"
#include "PhysicsLib/Actors/PhysicsDynamicActor.h"
#include "PhysicsLib/Actors/PhysicsKinematicActor.h"
#include "PhysicsLib/Actors/PhysicsStaticActor.h"
#include "PhysicsLib/Actors/PhysicsTerrainActor.h"

class IPhysicsActorRepository {
public:
    IPhysicsActorRepository();
    virtual ~IPhysicsActorRepository();
    IPhysicsActorRepository(const IPhysicsActorRepository& Other);
    IPhysicsActorRepository& operator=(const IPhysicsActorRepository& Other);
    IPhysicsActorRepository(IPhysicsActorRepository&& Other) noexcept;
    IPhysicsActorRepository& operator=(IPhysicsActorRepository&& Other) noexcept;

public:
    virtual std::unique_ptr<IPhysicsActorRepository> Clone() const = 0;

    virtual PhysicsDynamicActor* CreateDynamicActor(const PhysicsDynamicActor::ActorDesc& Desc) = 0;
    virtual PhysicsKinematicActor* CreateKinematicActor(const PhysicsKinematicActor::ActorDesc& Desc) = 0;
    virtual PhysicsTerrainActor* CreateTerrainActor(const PhysicsTerrainActor::ActorDesc& Desc) = 0;

    virtual void AddActor(std::unique_ptr<PhysicsActorBase> Actor) = 0;
    virtual void ClearActors() = 0;

    virtual PhysicsActorBase* GetActor(std::size_t Index) = 0;
    virtual const PhysicsActorBase* GetActor(std::size_t Index) const = 0;
    virtual std::size_t GetActorCount() const = 0;

    virtual std::vector<PhysicsDynamicActor*> CollectDynamicActors() = 0;
    virtual std::vector<const PhysicsDynamicActor*> CollectDynamicActors() const = 0;
    virtual std::vector<PhysicsKinematicActor*> CollectKinematicActors() = 0;
    virtual std::vector<const PhysicsKinematicActor*> CollectKinematicActors() const = 0;
    virtual std::vector<const PhysicsStaticActor*> CollectStaticActors() const = 0;
};
