#pragma once

#include "Arche/Common.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        BoneSkinReference,
        ComponentFields(
            ComponentField(Arche::EntityID, boneRootEntityId, Arche::NullEntityID)
        ),
        BOOST_PP_SEQ_NIL
    );
}
