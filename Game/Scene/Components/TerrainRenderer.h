#pragma once

#include <cstdint>
#include "Game/Model/TerrainRenderResource.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    constexpr std::uint32_t InvalidTerrainTileMetadataIndex{ 0xffffffffu };

    ComponentDecl(
        TerrainRenderer,
        ComponentFields(
            ComponentField(TerrainRenderResource*, mResource, nullptr)
            ComponentField(std::uint32_t, mTileQuadCount, 56)
            ComponentField(std::uint32_t, mTileMetadataIndex, InvalidTerrainTileMetadataIndex)
            ComponentField(bool, mActive, true)
        ),
        BOOST_PP_SEQ_NIL
    );
}
