#pragma once
#include <string>
#include <vector>
#include "Scene.h"

#ifdef min
#undef min
#endif

#ifdef max 
#undef max
#endif

#pragma comment(lib, "c4core.lib")
#pragma comment(lib, "ryml.lib")

namespace Game {
    struct SceneYamlSaveResult final {
    public:
        SceneYamlSaveResult();
        ~SceneYamlSaveResult();
        SceneYamlSaveResult(const SceneYamlSaveResult& Other);
        SceneYamlSaveResult& operator=(const SceneYamlSaveResult& Other);
        SceneYamlSaveResult(SceneYamlSaveResult&& Other) noexcept;
        SceneYamlSaveResult& operator=(SceneYamlSaveResult&& Other) noexcept;

    public:
        bool IsSuccess{ true };
        std::vector<std::string> UndecidedItems{};
    };

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
        SceneYamlLoadResult Deserialize(const std::string& YamlText, Scene& OutScene) const;
        SceneYamlLoadResult DeserializeFromFile(const std::string& YamlFilePath, Scene& OutScene) const;

        SceneYamlSaveResult Serialize(const Scene& TargetScene, std::string& OutYamlText) const;
        SceneYamlSaveResult Serialize(const SceneWorldSnapshot& TargetSnapshot, std::string& OutYamlText) const;

        SceneYamlSaveResult SerializeToFile(const Scene& TargetScene, const std::string& YamlFilePath) const;
        SceneYamlSaveResult SerializeToFile(const SceneWorldSnapshot& TargetSnapshot, const std::string& YamlFilePath) const;
    };
}
