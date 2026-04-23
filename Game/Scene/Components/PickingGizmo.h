#pragma once

#include <cstdint>
#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        PickingGizmo,
        ComponentFields(
            ComponentField(std::uint32_t, axisIndex, 0)
        ),
        BOOST_PP_SEQ_NIL
    );
}
