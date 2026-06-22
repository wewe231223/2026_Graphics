#pragma once
#include "Arche/Common.h"
#include "Terrain/TerrainQuery.h"
#include "PhysicsLib/Actors/PhysicsTerrainActor.h"

namespace Game {
    struct TerrainActorDescBinding final {
        Arche::EntityID mEntityId{ Arche::NullEntityID };
        PhysicsTerrainActor::ActorDesc mTerrainActorDesc{};
        Terrain::TerrainDataHandle mTerrainHandle{};
        bool mIsTerrainActorDescApplied{};
    };
}
