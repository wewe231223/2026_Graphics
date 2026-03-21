#pragma once

#include "Arche/Common.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    Component(BoneSkinReference)
        Arche::EntityID boneRootEntityId{ Arche::NullEntityID };
    EndComponent(BoneSkinReference)
}
