#pragma once

#include <cstdint>
#include "Game/Model/Model.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        StaticMeshRenderer,
        ComponentFields(
            ComponentField(Model*, model, nullptr)
            ComponentField(::std::uint32_t, nodeIndex, 0)
            ComponentField(bool, active, true)
        ),
        BOOST_PP_SEQ_NIL
    );
}
