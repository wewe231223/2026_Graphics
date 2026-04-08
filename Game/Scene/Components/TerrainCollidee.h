#pragma once

#include "Game/Scene/TerrainHeightResolver.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        TerrainCollidee,
        ComponentFields(
            ComponentField(TerrainHeightResolver*, mTerrainHeightResolver, nullptr)
        ),
        BOOST_PP_SEQ_NIL
    );
}
