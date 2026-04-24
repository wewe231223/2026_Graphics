#include <utility>

#include "PhysicsLib/Actors/Integrater/PhysicsStaticIntegrater.h"
#include "PhysicsLib/Actors/PhysicsActorBase.h"
#include "PhysicsLib/Simulation/Mediator/IPhysicsWorldMediator.h"

PhysicsStaticIntegrater::PhysicsStaticIntegrater() {
}

PhysicsStaticIntegrater::~PhysicsStaticIntegrater() {
}

PhysicsStaticIntegrater::PhysicsStaticIntegrater(const PhysicsStaticIntegrater& Other)
    : IPhysicsIntegrater{ Other } {
}

PhysicsStaticIntegrater& PhysicsStaticIntegrater::operator=(const PhysicsStaticIntegrater& Other) {
    if (this == &Other) {
        return *this;
    }

    IPhysicsIntegrater::operator=(Other);
    return *this;
}

PhysicsStaticIntegrater::PhysicsStaticIntegrater(PhysicsStaticIntegrater&& Other) noexcept
    : IPhysicsIntegrater{ std::move(Other) } {
}

PhysicsStaticIntegrater& PhysicsStaticIntegrater::operator=(PhysicsStaticIntegrater&& Other) noexcept {
    if (this == &Other) {
        return *this;
    }

    IPhysicsIntegrater::operator=(std::move(Other));
    return *this;
}

void PhysicsStaticIntegrater::Integrate(IPhysicsWorldMediator& WorldMediator, PhysicsActorBase& Actor, float DeltaTime) const {
    (void)WorldMediator;
    (void)Actor;
    (void)DeltaTime;
}
