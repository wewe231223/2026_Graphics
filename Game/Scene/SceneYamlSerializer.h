#pragma once
#include <string>
#include <vector>
#include "Scene.h"
#include "Game/Model/AssetRegistry.h"

#ifdef min
#undef min
#endif

#ifdef max 
#undef max
#endif

#pragma comment(lib, "c4core.lib")
#pragma comment(lib, "ryml.lib")

namespace Game {
    struct SceneYamlLoadResult final {
    public:
        SceneYamlLoadResult();
        ~SceneYamlLoadResult();
        SceneYamlLoadResult(const SceneYamlLoadResult& Other);
        SceneYamlLoadResult& operator=(const SceneYamlLoadResult& Other);
        SceneYamlLoadResult(SceneYamlLoadResult&& Other) noexcept;
        SceneYamlLoadResult& operator=(SceneYamlLoadResult&& Other) noexcept;

    public:
        bool IsSuccess{ true };
        std::vector<std::string> UndecidedItems{};
    };

    class SceneYamlSerializer final {
    public:
        SceneYamlSerializer();
        ~SceneYamlSerializer();
        SceneYamlSerializer(const SceneYamlSerializer& Other);
        SceneYamlSerializer& operator=(const SceneYamlSerializer& Other);
        SceneYamlSerializer(SceneYamlSerializer&& Other) noexcept;
        SceneYamlSerializer& operator=(SceneYamlSerializer&& Other) noexcept;

    public:
        SceneYamlLoadResult Deserialize(const std::string& YamlText, AssetRegistry& Registry, Scene& OutScene) const;
        SceneYamlLoadResult DeserializeFromFile(const std::string& YamlFilePath, AssetRegistry& Registry, Scene& OutScene) const;
    };
}
