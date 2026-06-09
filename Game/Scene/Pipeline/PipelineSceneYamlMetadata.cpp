#include "PipelineSceneYamlMetadata.h"
#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <ryml.hpp>
#include <ryml_std.hpp>

namespace Game {
    namespace Pipeline {
        namespace {
            std::string TrimCopy(const std::string& Text);
            PipelineSceneYamlMetadataLoadResult ReadPipelineMetadata(const std::string& YamlText, std::vector<SerializedUnitPipelineAssignment>& OutAssignments);

            std::string TrimCopy(const std::string& Text) {
                const std::string::const_iterator BeginIter{ std::find_if(Text.begin(), Text.end(), [](unsigned char Ch) { return std::isspace(Ch) == 0; }) };
                const std::string::const_reverse_iterator EndIter{ std::find_if(Text.rbegin(), Text.rend(), [](unsigned char Ch) { return std::isspace(Ch) == 0; }) };
                if (BeginIter == Text.end()) {
                    return {};
                }

                return std::string{ BeginIter, EndIter.base() };
            }

            PipelineSceneYamlMetadataLoadResult ReadPipelineMetadata(const std::string& YamlText, std::vector<SerializedUnitPipelineAssignment>& OutAssignments) {
                PipelineSceneYamlMetadataLoadResult LoadResult{};
                std::vector<SerializedUnitPipelineAssignment> Assignments{};

                c4::yml::Tree Tree{};
                try {
                    Tree = c4::yml::parse_in_arena(c4::to_csubstr(YamlText));
                    Tree.resolve();
                }
                catch (const std::exception& Exception) {
                    LoadResult.IsSuccess = false;
                    LoadResult.UndecidedItems.push_back(std::string{ "Scene YAML Pipeline metadata parse failed: " } + Exception.what());
                    return LoadResult;
                }
                catch (...) {
                    LoadResult.IsSuccess = false;
                    LoadResult.UndecidedItems.push_back("Scene YAML Pipeline metadata parse failed.");
                    return LoadResult;
                }

                const c4::yml::ConstNodeRef RootNode{ Tree.rootref() };
                if (RootNode.has_child("Entities") == false) {
                    OutAssignments.clear();
                    return LoadResult;
                }

                const c4::yml::ConstNodeRef EntitiesNode{ RootNode["Entities"] };
                if (EntitiesNode.is_seq() == false) {
                    LoadResult.IsSuccess = false;
                    LoadResult.UndecidedItems.push_back("Entities must be a sequence.");
                    return LoadResult;
                }

                std::unordered_set<std::int64_t> SerializedEntityIds{};
                for (const c4::yml::ConstNodeRef EntityNode : EntitiesNode.children()) {
                    if (EntityNode.is_map() == false) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back("Entity node must be a map.");
                        continue;
                    }

                    if (EntityNode.has_child("Pipeline") == false) {
                        continue;
                    }

                    std::int64_t SerializedEntityId{ -1 };
                    if (EntityNode.has_child("EntityId") == false) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back("Pipeline metadata EntityId is missing.");
                        continue;
                    }

                    EntityNode["EntityId"] >> SerializedEntityId;
                    const bool IsSerializedEntityIdInserted{ SerializedEntityIds.insert(SerializedEntityId).second };
                    if (IsSerializedEntityIdInserted == false) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back(std::string{ "Duplicate Pipeline metadata EntityId: " } + std::to_string(SerializedEntityId));
                        continue;
                    }

                    std::string PipelineName{};
                    EntityNode["Pipeline"] >> PipelineName;
                    PipelineName = TrimCopy(PipelineName);
                    if (PipelineName.empty() == true) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back(std::string{ "Pipeline name is empty: " } + std::to_string(SerializedEntityId));
                        continue;
                    }

                    SerializedUnitPipelineAssignment Assignment{};
                    Assignment.mSerializedEntityId = SerializedEntityId;
                    Assignment.mPipelineName = std::move(PipelineName);
                    Assignments.push_back(std::move(Assignment));
                }

                if (LoadResult.IsSuccess == true) {
                    OutAssignments = std::move(Assignments);
                }

                return LoadResult;
            }
        }

        PipelineSceneYamlMetadataLoadResult::PipelineSceneYamlMetadataLoadResult() = default;
        PipelineSceneYamlMetadataLoadResult::~PipelineSceneYamlMetadataLoadResult() = default;
        PipelineSceneYamlMetadataLoadResult::PipelineSceneYamlMetadataLoadResult(const PipelineSceneYamlMetadataLoadResult& Other) = default;
        PipelineSceneYamlMetadataLoadResult& PipelineSceneYamlMetadataLoadResult::operator=(const PipelineSceneYamlMetadataLoadResult& Other) = default;
        PipelineSceneYamlMetadataLoadResult::PipelineSceneYamlMetadataLoadResult(PipelineSceneYamlMetadataLoadResult&& Other) noexcept = default;
        PipelineSceneYamlMetadataLoadResult& PipelineSceneYamlMetadataLoadResult::operator=(PipelineSceneYamlMetadataLoadResult&& Other) noexcept = default;

        PipelineSceneYamlMetadata::PipelineSceneYamlMetadata() = default;
        PipelineSceneYamlMetadata::~PipelineSceneYamlMetadata() = default;
        PipelineSceneYamlMetadata::PipelineSceneYamlMetadata(const PipelineSceneYamlMetadata& Other) = default;
        PipelineSceneYamlMetadata& PipelineSceneYamlMetadata::operator=(const PipelineSceneYamlMetadata& Other) = default;
        PipelineSceneYamlMetadata::PipelineSceneYamlMetadata(PipelineSceneYamlMetadata&& Other) noexcept = default;
        PipelineSceneYamlMetadata& PipelineSceneYamlMetadata::operator=(PipelineSceneYamlMetadata&& Other) noexcept = default;

        PipelineSceneYamlMetadataLoadResult PipelineSceneYamlMetadata::Deserialize(const std::string& YamlText, std::vector<SerializedUnitPipelineAssignment>& OutAssignments) const {
            return ReadPipelineMetadata(YamlText, OutAssignments);
        }

        PipelineSceneYamlMetadataLoadResult PipelineSceneYamlMetadata::DeserializeFromFile(const std::string& YamlFilePath, std::vector<SerializedUnitPipelineAssignment>& OutAssignments) const {
            std::ifstream InputStream{ YamlFilePath, std::ios::in | std::ios::binary };
            PipelineSceneYamlMetadataLoadResult LoadResult{};
            if (InputStream.is_open() == false) {
                LoadResult.IsSuccess = false;
                LoadResult.UndecidedItems.push_back(std::string{ "Scene YAML file could not be opened: " } + YamlFilePath);
                return LoadResult;
            }

            std::stringstream Buffer{};
            Buffer << InputStream.rdbuf();
            return Deserialize(Buffer.str(), OutAssignments);
        }
    }
}
