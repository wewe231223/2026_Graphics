#pragma once

#include "Game/Scene/SceneYaml/SceneYamlComponentRegistry.h"

namespace Game::SceneYaml {
    class SceneYamlTerrainComponentReader final {
    public:
        static const char* TypeName();
        static void Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState);
    };

    class SceneYamlTerrainComponentWriter final {
    public:
        static const char* TypeName();
        static void Write(const SceneYamlComponentWriteContext& WriteContext);
    };
}
