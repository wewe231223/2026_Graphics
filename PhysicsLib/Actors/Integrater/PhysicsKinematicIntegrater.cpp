#include <utility>

#include "PhysicsLib/Actors/Integrater/PhysicsKinematicIntegrater.h"

PhysicsKinematicIntegrater::PhysicsKinematicIntegrater() {
}

PhysicsKinematicIntegrater::~PhysicsKinematicIntegrater() {
}

PhysicsKinematicIntegrater::PhysicsKinematicIntegrater(const PhysicsKinematicIntegrater& Other)
    : IPhysicsIntegrater{ Other } {
}

PhysicsKinematicIntegrater& PhysicsKinematicIntegrater::operator=(const PhysicsKinematicIntegrater& Other) {
    if (this == &Other) {
        return *this;
    }

    IPhysicsIntegrater::operator=(Other);
    return *this;
}

PhysicsKinematicIntegrater::PhysicsKinematicIntegrater(PhysicsKinematicIntegrater&& Other) noexcept
    : IPhysicsIntegrater{ std::move(Other) } {
}

PhysicsKinematicIntegrater& PhysicsKinematicIntegrater::operator=(PhysicsKinematicIntegrater&& Other) noexcept {
    if (this == &Other) {
        return *this;
    }

    IPhysicsIntegrater::operator=(std::move(Other));
    return *this;
}

void PhysicsKinematicIntegrater::Integrate(IPhysicsWorldMediator& WorldMediator, PhysicsActorBase& Actor, float DeltaTime) const {
    (void)WorldMediator;
    (void)Actor;
    (void)DeltaTime;
}
