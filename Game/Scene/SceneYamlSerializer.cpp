#include "SceneYamlSerializer.h"
#include <fstream>
#include <sstream>
#include "Game/Scene/SceneYaml/SceneYamlInternal.h"

namespace Game {
    SceneYamlSaveResult::SceneYamlSaveResult() = default;
    SceneYamlSaveResult::~SceneYamlSaveResult() = default;
    SceneYamlSaveResult::SceneYamlSaveResult(const SceneYamlSaveResult& Other) = default;
    SceneYamlSaveResult& SceneYamlSaveResult::operator=(const SceneYamlSaveResult& Other) = default;
    SceneYamlSaveResult::SceneYamlSaveResult(SceneYamlSaveResult&& Other) noexcept = default;
    SceneYamlSaveResult& SceneYamlSaveResult::operator=(SceneYamlSaveResult&& Other) noexcept = default;

    SceneYamlLoadResult::SceneYamlLoadResult() = default;
    SceneYamlLoadResult::~SceneYamlLoadResult() = default;
    SceneYamlLoadResult::SceneYamlLoadResult(const SceneYamlLoadResult& Other) = default;
    SceneYamlLoadResult& SceneYamlLoadResult::operator=(const SceneYamlLoadResult& Other) = default;
    SceneYamlLoadResult::SceneYamlLoadResult(SceneYamlLoadResult&& Other) noexcept = default;
    SceneYamlLoadResult& SceneYamlLoadResult::operator=(SceneYamlLoadResult&& Other) noexcept = default;

    SceneYamlSerializer::SceneYamlSerializer() = default;
    SceneYamlSerializer::~SceneYamlSerializer() = default;
    SceneYamlSerializer::SceneYamlSerializer(const SceneYamlSerializer& Other) = default;
    SceneYamlSerializer& SceneYamlSerializer::operator=(const SceneYamlSerializer& Other) = default;
    SceneYamlSerializer::SceneYamlSerializer(SceneYamlSerializer&& Other) noexcept = default;
    SceneYamlSerializer& SceneYamlSerializer::operator=(SceneYamlSerializer&& Other) noexcept = default;

    SceneYamlLoadResult SceneYamlSerializer::Deserialize(const std::string& YamlText, Scene& OutScene) const {
        const SceneYaml::SceneYamlDeserializer Deserializer{};
        return Deserializer.Deserialize(YamlText, OutScene);
    }
    SceneYamlLoadResult SceneYamlSerializer::DeserializeFromFile(const std::string& YamlFilePath, Scene& OutScene) const {
        std::ifstream InputStream{ YamlFilePath, std::ios::in | std::ios::binary };
        SceneYamlLoadResult LoadResult{};

        if (InputStream.is_open() == false) {
            LoadResult.IsSuccess = false;
            LoadResult.UndecidedItems.push_back(std::string{ "YAML 파일을 열 수 없습니다: " } + YamlFilePath);
            return LoadResult;
        }

        std::stringstream Buffer{};
        Buffer << InputStream.rdbuf();
        return Deserialize(Buffer.str(), OutScene);
    }
    SceneYamlSaveResult SceneYamlSerializer::Serialize(const Scene& TargetScene, std::string& OutYamlText) const {
        return Serialize(TargetScene.GetWorldSnapshot(), OutYamlText);
    }

    SceneYamlSaveResult SceneYamlSerializer::Serialize(const SceneWorldSnapshot& TargetSnapshot, std::string& OutYamlText) const {
        const SceneYaml::SceneYamlWriter Writer{};
        return Writer.Serialize(TargetSnapshot, OutYamlText);
    }
    SceneYamlSaveResult SceneYamlSerializer::SerializeToFile(const Scene& TargetScene, const std::string& YamlFilePath) const {
        return SerializeToFile(TargetScene.GetWorldSnapshot(), YamlFilePath);
    }

    SceneYamlSaveResult SceneYamlSerializer::SerializeToFile(const SceneWorldSnapshot& TargetSnapshot, const std::string& YamlFilePath) const {
        std::string YamlText{};
        SceneYamlSaveResult SaveResult{ Serialize(TargetSnapshot, YamlText) };

        std::ofstream OutputStream{ YamlFilePath, std::ios::out | std::ios::binary | std::ios::trunc };
        if (OutputStream.is_open() == false) {
            SaveResult.IsSuccess = false;
            SaveResult.UndecidedItems.push_back(std::string{ "YAML 파일을 쓸 수 없습니다: " } + YamlFilePath);
            return SaveResult;
        }

        OutputStream << YamlText;
        if (OutputStream.good() == false) {
            SaveResult.IsSuccess = false;
            SaveResult.UndecidedItems.push_back(std::string{ "YAML 파일 쓰기 실패: " } + YamlFilePath);
        }

        return SaveResult;
    }
}
