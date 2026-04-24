#pragma once

/*
PhysicsLib Header Guide
Role:
- Provides the default unique_ptr based Actor repository implementation.
Initialization:
- Default construct it for tests or let PhysicsWorld create it as an internal dependency.
Usage:
- Register Actors through creation functions or AddActor, then use index lookup and typed collection functions.
Notes:
- Clone calls each Actor Clone implementation, so custom Actors must implement Clone correctly.
*/

#include <cstddef>
#include <memory>
#include <vector>

#include "IPhysicsActorRepository.h"

class PhysicsActorRepository final : public IPhysicsActorRepository {
public:
    PhysicsActorRepository();
    ~PhysicsActorRepository() override;
    PhysicsActorRepository(const PhysicsActorRepository& Other);
    PhysicsActorRepository& operator=(const PhysicsActorRepository& Other);
    PhysicsActorRepository(PhysicsActorRepository&& Other) noexcept;
    PhysicsActorRepository& operator=(PhysicsActorRepository&& Other) noexcept;

public:
    std::unique_ptr<IPhysicsActorRepository> Clone() const override;

    PhysicsDynamicActor* CreateDynamicActor(const PhysicsDynamicActor::ActorDesc& Desc) override;
    PhysicsKinematicActor* CreateKinematicActor(const PhysicsKinematicActor::ActorDesc& Desc) override;
    PhysicsTerrainActor* CreateTerrainActor(const PhysicsTerrainActor::ActorDesc& Desc) override;

    void AddActor(std::unique_ptr<PhysicsActorBase> Actor) override;
    void ClearActors() override;

    PhysicsActorBase* GetActor(std::size_t Index) override;
    const PhysicsActorBase* GetActor(std::size_t Index) const override;
    std::size_t GetActorCount() const override;

    std::vector<PhysicsDynamicActor*> CollectDynamicActors() override;
    std::vector<const PhysicsDynamicActor*> CollectDynamicActors() const override;
    std::vector<PhysicsKinematicActor*> CollectKinematicActors() override;
    std::vector<const PhysicsKinematicActor*> CollectKinematicActors() const override;
    std::vector<const PhysicsStaticActor*> CollectStaticActors() const override;

private:
    std::vector<std::unique_ptr<PhysicsActorBase>> mActors;
};
