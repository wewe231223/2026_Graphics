#pragma once

class IPhysicsActorRepository;
class IPhysicsWorldMediator;

class PhysicsKinematicActorSimulator final {
public:
    PhysicsKinematicActorSimulator();
    ~PhysicsKinematicActorSimulator();
    PhysicsKinematicActorSimulator(const PhysicsKinematicActorSimulator& Other);
    PhysicsKinematicActorSimulator& operator=(const PhysicsKinematicActorSimulator& Other);
    PhysicsKinematicActorSimulator(PhysicsKinematicActorSimulator&& Other) noexcept;
    PhysicsKinematicActorSimulator& operator=(PhysicsKinematicActorSimulator&& Other) noexcept;

public:
    void Tick(IPhysicsWorldMediator& WorldMediator, IPhysicsActorRepository& ActorRepository, float DeltaTime);
};
