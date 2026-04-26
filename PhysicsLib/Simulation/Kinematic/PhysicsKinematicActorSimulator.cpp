#include "PhysicsLib/Simulation/Kinematic/PhysicsKinematicActorSimulator.h"

#include <cstddef>
#include <vector>

#include "PhysicsLib/Actors/PhysicsKinematicActor.h"
#include "PhysicsLib/Simulation/Mediator/IPhysicsWorldMediator.h"
#include "PhysicsLib/Simulation/Repository/IPhysicsActorRepository.h"

PhysicsKinematicActorSimulator::PhysicsKinematicActorSimulator() = default;

PhysicsKinematicActorSimulator::~PhysicsKinematicActorSimulator() = default;

PhysicsKinematicActorSimulator::PhysicsKinematicActorSimulator(const PhysicsKinematicActorSimulator& Other) = default;

PhysicsKinematicActorSimulator& PhysicsKinematicActorSimulator::operator=(const PhysicsKinematicActorSimulator& Other) = default;

PhysicsKinematicActorSimulator::PhysicsKinematicActorSimulator(PhysicsKinematicActorSimulator&& Other) noexcept = default;

PhysicsKinematicActorSimulator& PhysicsKinematicActorSimulator::operator=(PhysicsKinematicActorSimulator&& Other) noexcept = default;

void PhysicsKinematicActorSimulator::Tick(IPhysicsWorldMediator& WorldMediator, IPhysicsActorRepository& ActorRepository, float DeltaTime) {
    if (DeltaTime <= 0.0F) {
        return;
    }

    std::vector<PhysicsKinematicActor*> KinematicActors{ ActorRepository.CollectKinematicActors() };
    std::size_t KinematicActorCount{ KinematicActors.size() };
    for (std::size_t ActorIndex{ 0U }; ActorIndex < KinematicActorCount; ++ActorIndex) {
        PhysicsKinematicActor* KinematicActor{ KinematicActors[ActorIndex] };
        if (KinematicActor == nullptr) {
            continue;
        }

        KinematicActor->Integrate(WorldMediator, DeltaTime);
        KinematicActor->SolveConstraints(WorldMediator, DeltaTime);
    }
}
