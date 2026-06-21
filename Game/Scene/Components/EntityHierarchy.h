#pragma once

#include "Arche/Common.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        EntityHierarchy,
        ComponentFields(
            ComponentField(Arche::EntityID, self, Arche::NullEntityID)
            ComponentField(Arche::EntityID, parent, Arche::NullEntityID)
            ComponentField(Arche::EntityID, firstChild, Arche::NullEntityID)
            ComponentField(Arche::EntityID, nextSibling, Arche::NullEntityID)
        ),
        BOOST_PP_SEQ_NIL
    );
}
