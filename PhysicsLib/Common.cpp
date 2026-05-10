#include "PhysicsLib/Common.h"

#include <utility>

IPhysicsWorld::IPhysicsWorld() {
}

IPhysicsWorld::~IPhysicsWorld() {
}

IPhysicsWorld::IPhysicsWorld(const IPhysicsWorld& Other)
    : IPhysicsWorldMediator{ Other } {
}

IPhysicsWorld& IPhysicsWorld::operator=(const IPhysicsWorld& Other) {
    if (this == &Other) {
        return *this;
    }

    IPhysicsWorldMediator::operator=(Other);
    return *this;
}

IPhysicsWorld::IPhysicsWorld(IPhysicsWorld&& Other) noexcept
    : IPhysicsWorldMediator{ std::move(Other) } {
}

IPhysicsWorld& IPhysicsWorld::operator=(IPhysicsWorld&& Other) noexcept {
    if (this == &Other) {
        return *this;
    }

    IPhysicsWorldMediator::operator=(std::move(Other));
    return *this;
}
