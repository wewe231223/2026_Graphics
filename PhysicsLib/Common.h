#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <DirectXTK12/SimpleMath.h>

#include "PhysicsLib/Actors/PhysicsDynamicActor.h"
#include "PhysicsLib/Actors/PhysicsKinematicActor.h"
#include "PhysicsLib/Actors/PhysicsTerrainActor.h"
#include "PhysicsLib/Actors/SolverType/PhysicsSolverType.h"
#include "PhysicsLib/Simulation/Mediator/IPhysicsWorldMediator.h"
#include "PhysicsLib/Simulation/Repository/IPhysicsActorRepository.h"
#include "PhysicsLib/Simulation/SpatialQuery/IPhysicsSpatialQuery.h"
#include "PhysicsLib/Simulation/Types/PhysicsSimulationTypes.h"

struct PhysicsCharacterMoveRequest final {
    DirectX::SimpleMath::Vector3 mDisplacement{};
    float mGroundSnapDistance{};
};

struct PhysicsCharacterMoveResult final {
    DirectX::SimpleMath::Vector3 mPosition{};
    DirectX::SimpleMath::Vector3 mVelocity{};
    bool mIsGrounded{};
};

class IPhysicsWorld : public IPhysicsWorldMediator {
public:
    struct WorldSettings {
        float FixedTimeStep{};
        DirectX::SimpleMath::Vector3 Gravity{};
        float KinematicDynamicCcdImpulseMagnitudeClamp{ 1000.0F };
    };

public:
    IPhysicsWorld();
    ~IPhysicsWorld() override;
    IPhysicsWorld(const IPhysicsWorld& Other);
    IPhysicsWorld& operator=(const IPhysicsWorld& Other);
    IPhysicsWorld(IPhysicsWorld&& Other) noexcept;
    IPhysicsWorld& operator=(IPhysicsWorld&& Other) noexcept;

public:
    virtual void Initialize(const WorldSettings& Settings) = 0;

    virtual PhysicsDynamicActor* CreateDynamicActor(const PhysicsDynamicActor::ActorDesc& Desc) = 0;
    virtual PhysicsKinematicActor* CreateKinematicActor(const PhysicsKinematicActor::ActorDesc& Desc) = 0;
    virtual PhysicsTerrainActor* CreateTerrainActor(const PhysicsTerrainActor::ActorDesc& Desc) = 0;
    virtual void AddActor(std::unique_ptr<PhysicsActorBase> Actor) = 0;
    virtual void ClearActors() = 0;

    virtual PhysicsActorBase* GetActor(std::size_t Index) = 0;
    virtual const PhysicsActorBase* GetActor(std::size_t Index) const = 0;
    virtual PhysicsTerrainActor* GetTerrainActor(std::size_t Index) = 0;
    virtual const PhysicsTerrainActor* GetTerrainActor(std::size_t Index) const = 0;
    virtual std::size_t GetActorCount() const = 0;
    virtual std::vector<PhysicsTerrainActor*> CollectTerrainActors() = 0;
    virtual std::vector<const PhysicsTerrainActor*> CollectTerrainActors() const = 0;

    virtual const WorldSettings& GetSettings() const = 0;
    virtual float GetAccumulator() const = 0;
    virtual float GetInterpolationAlpha() const = 0;
    virtual std::size_t GetLastUpdateStepCount() const = 0;
    virtual double GetLastUpdateStepElapsedMilliseconds() const = 0;
    virtual double GetLastStepElapsedMilliseconds() const = 0;
    virtual bool TryGetInterpolatedActorTransform(const PhysicsActorBase& Actor, DirectX::SimpleMath::Vector3& OutPosition, DirectX::SimpleMath::Quaternion& OutOrientation, DirectX::SimpleMath::Vector3& OutScale) const = 0;

    virtual void TickKinematicActors(float DeltaTime) = 0;
    virtual void MarkKinematicActorTeleported(const PhysicsKinematicActor& Actor) = 0;
    virtual void StepSimulation() = 0;
    virtual void Update(float DeltaTime) = 0;
    virtual PhysicsCharacterMoveResult MoveKinematicCharacter(PhysicsActorBase& Actor, const PhysicsCharacterMoveRequest& Request, float DeltaTime) = 0;
    virtual bool ResolveKinematicTerrainContact(PhysicsActorBase& Actor) = 0;
    virtual void ResolveKinematicTerrainContacts() = 0;
};
