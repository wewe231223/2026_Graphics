#pragma once
#include <string>
#include <vector>
#include "Game/Scene/Base/Scene.h"
#include "Game/Scene/Base/Types.h"

namespace Game {
    namespace Pipeline {
        struct PipelineYamlSaveResult final {
        public:
            PipelineYamlSaveResult();
            ~PipelineYamlSaveResult();

            PipelineYamlSaveResult(const PipelineYamlSaveResult& Other);
            PipelineYamlSaveResult& operator=(const PipelineYamlSaveResult& Other);

            PipelineYamlSaveResult(PipelineYamlSaveResult&& Other) noexcept;
            PipelineYamlSaveResult& operator=(PipelineYamlSaveResult&& Other) noexcept;

        public:
            bool IsSuccess{ true };
            std::vector<std::string> UndecidedItems{};
        };

        struct PipelineYamlLoadResult final {
        public:
            PipelineYamlLoadResult();
            ~PipelineYamlLoadResult();

            PipelineYamlLoadResult(const PipelineYamlLoadResult& Other);
            PipelineYamlLoadResult& operator=(const PipelineYamlLoadResult& Other);

            PipelineYamlLoadResult(PipelineYamlLoadResult&& Other) noexcept;
            PipelineYamlLoadResult& operator=(PipelineYamlLoadResult&& Other) noexcept;

        public:
            bool IsSuccess{ true };
            std::vector<std::string> UndecidedItems{};
        };

        class PipelineYamlSerializer final {
        public:
            PipelineYamlSerializer();
            ~PipelineYamlSerializer();

            PipelineYamlSerializer(const PipelineYamlSerializer& Other);
            PipelineYamlSerializer& operator=(const PipelineYamlSerializer& Other);

            PipelineYamlSerializer(PipelineYamlSerializer&& Other) noexcept;
            PipelineYamlSerializer& operator=(PipelineYamlSerializer&& Other) noexcept;

        public:
            PipelineYamlLoadResult Deserialize(const std::string& YamlText, std::vector<PipelineDefinition>& OutPipelineDefinitions) const;
            PipelineYamlLoadResult Deserialize(const std::string& YamlText, Scene& OutScene) const;
            PipelineYamlLoadResult DeserializeFromFile(const std::string& YamlFilePath, Scene& OutScene) const;

            PipelineYamlSaveResult Serialize(const std::vector<PipelineDefinition>& PipelineDefinitions, std::string& OutYamlText) const;
            PipelineYamlSaveResult Serialize(const Scene& TargetScene, std::string& OutYamlText) const;
            PipelineYamlSaveResult SerializeToFile(const Scene& TargetScene, const std::string& YamlFilePath) const;
        };
    }
}
