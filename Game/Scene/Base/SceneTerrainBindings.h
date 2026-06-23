#pragma once
#include "Arche/Common.h"
#include "PhysicsLib/Actors/PhysicsTerrainActor.h"

namespace Game {
    struct TerrainActorDescBinding final {
        Arche::EntityID mEntityId{ Arche::NullEntityID };
        PhysicsTerrainActor::ActorDesc mTerrainActorDesc{};
        bool mIsTerrainActorDescApplied{};
    };
}
