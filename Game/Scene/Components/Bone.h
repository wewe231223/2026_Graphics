#pragma once

#include <cstdint>
#include "Game/Model/Model.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        Bone,
        ComponentFields(
            ComponentField(Model*, model, nullptr)
            ComponentField(::std::uint32_t, nodeIndex, 0)
            ComponentField(::std::uint32_t, runtimeBoneInfoOffset, 0)
            ComponentField(::std::uint32_t, runtimeBoneInfoCount, 0)
        ),
        BOOST_PP_SEQ_NIL
    );
}
