#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Game {
    namespace Pipeline {
        struct SerializedUnitPipelineAssignment final {
            std::int64_t mSerializedEntityId{ -1 };
            std::string mPipelineName{};
        };

        struct PipelineSceneYamlMetadataLoadResult final {
        public:
            PipelineSceneYamlMetadataLoadResult();
            ~PipelineSceneYamlMetadataLoadResult();

            PipelineSceneYamlMetadataLoadResult(const PipelineSceneYamlMetadataLoadResult& Other);
            PipelineSceneYamlMetadataLoadResult& operator=(const PipelineSceneYamlMetadataLoadResult& Other);

            PipelineSceneYamlMetadataLoadResult(PipelineSceneYamlMetadataLoadResult&& Other) noexcept;
            PipelineSceneYamlMetadataLoadResult& operator=(PipelineSceneYamlMetadataLoadResult&& Other) noexcept;

        public:
            bool IsSuccess{ true };
            std::vector<std::string> UndecidedItems{};
        };

        class PipelineSceneYamlMetadata final {
        public:
            PipelineSceneYamlMetadata();
            ~PipelineSceneYamlMetadata();

            PipelineSceneYamlMetadata(const PipelineSceneYamlMetadata& Other);
            PipelineSceneYamlMetadata& operator=(const PipelineSceneYamlMetadata& Other);

            PipelineSceneYamlMetadata(PipelineSceneYamlMetadata&& Other) noexcept;
            PipelineSceneYamlMetadata& operator=(PipelineSceneYamlMetadata&& Other) noexcept;

        public:
            PipelineSceneYamlMetadataLoadResult Deserialize(const std::string& YamlText, std::vector<SerializedUnitPipelineAssignment>& OutAssignments) const;
            PipelineSceneYamlMetadataLoadResult DeserializeFromFile(const std::string& YamlFilePath, std::vector<SerializedUnitPipelineAssignment>& OutAssignments) const;
        };
    }
}
