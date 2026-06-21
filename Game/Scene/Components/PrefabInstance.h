#pragma once

#include <cstdint>
#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        PrefabInstance,
        ComponentFields(
            ComponentField(std::uint64_t, PrefabId, 0)
        ),
        BOOST_PP_SEQ_NIL
    );
}
