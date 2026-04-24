#pragma once

#include <cstdint>
#include "PhysicsLib/Actors/PhysicsActorBase.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        PhysicsActor,
        ComponentFields(
            ComponentField(PhysicsActorBase*, mActorPointer, nullptr)
            ComponentField(std::uint32_t, mActorIndex, 0)
            ComponentField(PhysicsActorBase::PhysicsActorType, mActorType, PhysicsActorBase::PhysicsActorType::Dynamic)
        ),
        ComponentMethods(
            ComponentMethod(bool HasActor() const, HasActor)
        )
    );
}
