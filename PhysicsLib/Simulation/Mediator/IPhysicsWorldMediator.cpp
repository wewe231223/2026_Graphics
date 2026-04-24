#include "IPhysicsWorldMediator.h"

IPhysicsWorldMediator::IPhysicsWorldMediator() {
}

IPhysicsWorldMediator::~IPhysicsWorldMediator() {
}

IPhysicsWorldMediator::IPhysicsWorldMediator(const IPhysicsWorldMediator& Other) {
    (void)Other;
}

IPhysicsWorldMediator& IPhysicsWorldMediator::operator=(const IPhysicsWorldMediator& Other) {
    (void)Other;
    return *this;
}

IPhysicsWorldMediator::IPhysicsWorldMediator(IPhysicsWorldMediator&& Other) noexcept {
    (void)Other;
}

IPhysicsWorldMediator& IPhysicsWorldMediator::operator=(IPhysicsWorldMediator&& Other) noexcept {
    (void)Other;
    return *this;
}
