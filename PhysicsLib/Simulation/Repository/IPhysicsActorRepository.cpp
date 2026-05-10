#include "IPhysicsActorRepository.h"

IPhysicsActorRepository::IPhysicsActorRepository() {
}

IPhysicsActorRepository::~IPhysicsActorRepository() {
}

IPhysicsActorRepository::IPhysicsActorRepository(const IPhysicsActorRepository& Other) {
    (void)Other;
}

IPhysicsActorRepository& IPhysicsActorRepository::operator=(const IPhysicsActorRepository& Other) {
    (void)Other;
    return *this;
}

IPhysicsActorRepository::IPhysicsActorRepository(IPhysicsActorRepository&& Other) noexcept {
    (void)Other;
}

IPhysicsActorRepository& IPhysicsActorRepository::operator=(IPhysicsActorRepository&& Other) noexcept {
    (void)Other;
    return *this;
}
