#pragma once

#include <vector>
#include <ryml.hpp>
#include "Arche/Common.h"
#include "SceneYamlContexts.h"

namespace Game::SceneYaml {
    struct SceneYamlComponentReader final {
    public:
        using TypeNameFunction = const char* (*)();
        using ReadFunction = void (*)(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState);

    public:
        TypeNameFunction mTypeName{};
        ReadFunction mRead{};
    };

    struct SceneYamlComponentWriter final {
    public:
        using TypeNameFunction = const char* (*)();
        using WriteFunction = void (*)(const SceneYamlComponentWriteContext& WriteContext);

    public:
        TypeNameFunction mTypeName{};
        WriteFunction mWrite{};
    };

    const std::vector<SceneYamlComponentReader>& GetSceneYamlPreModelComponentReaders();
    const std::vector<SceneYamlComponentReader>& GetSceneYamlModelComponentReaders();
    const std::vector<SceneYamlComponentReader>& GetSceneYamlPostModelComponentReaders();
    const std::vector<SceneYamlComponentWriter>& GetSceneYamlComponentWriters();
    void SpawnPrefabModelIfNeeded(Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState);
}
