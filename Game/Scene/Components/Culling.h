#pragma once

#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        Culling,
        ComponentFields(
            ComponentField(bool, frustumCulling, true)
        ),
        BOOST_PP_SEQ_NIL
    );
}
