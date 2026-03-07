#pragma once

#include "Arche/Common.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    Component(EntityHierarchy)
        Arche::EntityID self{ Arche::NullEntityID };
        Arche::EntityID parent{ Arche::NullEntityID };
        Arche::EntityID firstChild{ Arche::NullEntityID };
        Arche::EntityID nextSibling{ Arche::NullEntityID };
    EndComponent(EntityHierarchy)
}
