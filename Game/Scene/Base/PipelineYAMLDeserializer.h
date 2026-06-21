#pragma once
#include <string>
#include <vector>
#include "Game/Scene/Base/Scene.h"

namespace Game {
    namespace Pipeline {
        struct PipelineSceneYamlLoadResult final {
        public:
            PipelineSceneYamlLoadResult();
            ~PipelineSceneYamlLoadResult();

            PipelineSceneYamlLoadResult(const PipelineSceneYamlLoadResult& Other);
            PipelineSceneYamlLoadResult& operator=(const PipelineSceneYamlLoadResult& Other);

            PipelineSceneYamlLoadResult(PipelineSceneYamlLoadResult&& Other) noexcept;
            PipelineSceneYamlLoadResult& operator=(PipelineSceneYamlLoadResult&& Other) noexcept;

        public:
            bool IsSuccess{ true };
            std::vector<std::string> UndecidedItems{};
        };

        class PipelineSceneYamlDeserializer final {
        public:
            PipelineSceneYamlDeserializer();
            ~PipelineSceneYamlDeserializer();

            PipelineSceneYamlDeserializer(const PipelineSceneYamlDeserializer& Other);
            PipelineSceneYamlDeserializer& operator=(const PipelineSceneYamlDeserializer& Other);

            PipelineSceneYamlDeserializer(PipelineSceneYamlDeserializer&& Other) noexcept;
            PipelineSceneYamlDeserializer& operator=(PipelineSceneYamlDeserializer&& Other) noexcept;

        public:
            PipelineSceneYamlLoadResult Deserialize(const std::string& SceneYamlText, const std::string& PipelineYamlText, Scene& OutScene) const;
            PipelineSceneYamlLoadResult DeserializeFromFiles(const std::string& SceneYamlPath, const std::string& PipelineYamlPath, Scene& OutScene) const;

        private:
            void AppendUndecidedItems(PipelineSceneYamlLoadResult& TargetResult, const std::vector<std::string>& SourceItems) const;
            bool TryReadTextFile(const std::string& FilePath, std::string& OutText, PipelineSceneYamlLoadResult& OutResult) const;
            void ClearPipelineSceneLoadProducts(Scene& TargetScene) const;
        };
    }
}
