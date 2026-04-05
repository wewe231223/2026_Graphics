#pragma once

#include <cstdint>
#include "Arche/Common.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        SkySphere,
        ComponentFields(
            ComponentField(Arche::EntityID, SkySphereEntityId, Arche::NullEntityID)
            ComponentField(::std::int64_t, SkySphereSerializedEntityId, -1)
        ),
        BOOST_PP_SEQ_NIL
    );
}
