#pragma once

#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        TerrainCollider,
        ComponentFields(
            ComponentField(bool, mTerrainCollide, true)
        ),
        BOOST_PP_SEQ_NIL
    );
}
