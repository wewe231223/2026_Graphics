#include "PipelineYAMLDeserializer.h"
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include "Game/Scene/Base/PipelineYAMLMetadata.h"
#include "Game/Scene/Base/PipelineYAMLSerializer.h"
#include "Game/Scene/SceneYaml/SceneYamlTypes.h"

namespace Game {
    namespace Pipeline {
        namespace {
            void AppendUndecidedItems(PipelineSceneYamlLoadResult& TargetResult, const std::vector<std::string>& SourceItems);
            bool TryReadTextFile(const std::string& FilePath, std::string& OutText, PipelineSceneYamlLoadResult& OutResult);
            void ClearPipelineSceneLoadProducts(Scene& TargetScene);
            void AppendUndecidedItems(PipelineSceneYamlLoadResult& TargetResult, const std::vector<std::string>& SourceItems) {
                TargetResult.UndecidedItems.insert(TargetResult.UndecidedItems.end(), SourceItems.begin(), SourceItems.end());
            }

            bool TryReadTextFile(const std::string& FilePath, std::string& OutText, PipelineSceneYamlLoadResult& OutResult) {
                std::ifstream InputStream{ FilePath, std::ios::in | std::ios::binary };
                if (InputStream.is_open() == false) {
                    OutResult.IsSuccess = false;
                    OutResult.UndecidedItems.push_back(std::string{ "YAML file could not be opened: " } + FilePath);
                    return false;
                }

                std::stringstream Buffer{};
                Buffer << InputStream.rdbuf();
                OutText = Buffer.str();
                return true;
            }

            void ClearPipelineSceneLoadProducts(Scene& TargetScene) {
                TargetScene.ClearUnitPipelineAssignments();
                TargetScene.ClearWorkUnits();
                TargetScene.GetFrameContext().mRuntimePipelineAssignments.clear();
                TargetScene.GetFrameContext().mRuntimePipelineAssignmentVersion += 1ULL;
            }
        }

        PipelineSceneYamlLoadResult::PipelineSceneYamlLoadResult() = default;
        PipelineSceneYamlLoadResult::~PipelineSceneYamlLoadResult() = default;
        PipelineSceneYamlLoadResult::PipelineSceneYamlLoadResult(const PipelineSceneYamlLoadResult& Other) = default;
        PipelineSceneYamlLoadResult& PipelineSceneYamlLoadResult::operator=(const PipelineSceneYamlLoadResult& Other) = default;
        PipelineSceneYamlLoadResult::PipelineSceneYamlLoadResult(PipelineSceneYamlLoadResult&& Other) noexcept = default;
        PipelineSceneYamlLoadResult& PipelineSceneYamlLoadResult::operator=(PipelineSceneYamlLoadResult&& Other) noexcept = default;

        PipelineSceneYamlDeserializer::PipelineSceneYamlDeserializer() = default;
        PipelineSceneYamlDeserializer::~PipelineSceneYamlDeserializer() = default;
        PipelineSceneYamlDeserializer::PipelineSceneYamlDeserializer(const PipelineSceneYamlDeserializer& Other) = default;
        PipelineSceneYamlDeserializer& PipelineSceneYamlDeserializer::operator=(const PipelineSceneYamlDeserializer& Other) = default;
        PipelineSceneYamlDeserializer::PipelineSceneYamlDeserializer(PipelineSceneYamlDeserializer&& Other) noexcept = default;
        PipelineSceneYamlDeserializer& PipelineSceneYamlDeserializer::operator=(PipelineSceneYamlDeserializer&& Other) noexcept = default;

        PipelineSceneYamlLoadResult PipelineSceneYamlDeserializer::Deserialize(const std::string& SceneYamlText, const std::string& PipelineYamlText, Scene& OutScene) const {
            PipelineSceneYamlLoadResult LoadResult{};
            ClearPipelineSceneLoadProducts(OutScene);

            const PipelineYamlSerializer PipelineSerializer{};
            const PipelineYamlLoadResult PipelineLoadResult{ PipelineSerializer.Deserialize(PipelineYamlText, OutScene) };
            AppendUndecidedItems(LoadResult, PipelineLoadResult.UndecidedItems);
            if (PipelineLoadResult.IsSuccess == false) {
                LoadResult.IsSuccess = false;
                ClearPipelineSceneLoadProducts(OutScene);
                return LoadResult;
            }

            SceneYaml::SceneYamlDeserializer SceneDeserializer{};
            std::unordered_map<std::int64_t, Arche::EntityID> EntityIdMap{};
            const SceneYamlLoadResult SceneLoadResult{ SceneDeserializer.Deserialize(SceneYamlText, OutScene, EntityIdMap) };
            AppendUndecidedItems(LoadResult, SceneLoadResult.UndecidedItems);
            if (SceneLoadResult.IsSuccess == false) {
                LoadResult.IsSuccess = false;
                ClearPipelineSceneLoadProducts(OutScene);
                return LoadResult;
            }

            PipelineSceneYamlMetadata MetadataDeserializer{};
            std::vector<SerializedUnitPipelineAssignment> SerializedAssignments{};
            const PipelineSceneYamlMetadataLoadResult MetadataLoadResult{ MetadataDeserializer.Deserialize(SceneYamlText, SerializedAssignments) };
            AppendUndecidedItems(LoadResult, MetadataLoadResult.UndecidedItems);
            if (MetadataLoadResult.IsSuccess == false) {
                LoadResult.IsSuccess = false;
                ClearPipelineSceneLoadProducts(OutScene);
                return LoadResult;
            }

            const SceneWorkUnitBuildResult AssignmentApplyResult{ OutScene.ApplySerializedUnitPipelineAssignments(SerializedAssignments, EntityIdMap) };
            AppendUndecidedItems(LoadResult, AssignmentApplyResult.UndecidedItems);
            if (AssignmentApplyResult.IsSuccess == false) {
                LoadResult.IsSuccess = false;
                ClearPipelineSceneLoadProducts(OutScene);
                return LoadResult;
            }

            const SceneWorkUnitBuildResult WorkUnitBuildResult{ OutScene.RebuildWorkUnits() };
            AppendUndecidedItems(LoadResult, WorkUnitBuildResult.UndecidedItems);
            if (WorkUnitBuildResult.IsSuccess == false) {
                LoadResult.IsSuccess = false;
                ClearPipelineSceneLoadProducts(OutScene);
                return LoadResult;
            }

            return LoadResult;
        }

        PipelineSceneYamlLoadResult PipelineSceneYamlDeserializer::DeserializeFromFiles(const std::string& SceneYamlPath, const std::string& PipelineYamlPath, Scene& OutScene) const {
            PipelineSceneYamlLoadResult LoadResult{};
            std::string PipelineYamlText{};
            if (TryReadTextFile(PipelineYamlPath, PipelineYamlText, LoadResult) == false) {
                ClearPipelineSceneLoadProducts(OutScene);
                return LoadResult;
            }

            std::string SceneYamlText{};
            if (TryReadTextFile(SceneYamlPath, SceneYamlText, LoadResult) == false) {
                ClearPipelineSceneLoadProducts(OutScene);
                return LoadResult;
            }

            return Deserialize(SceneYamlText, PipelineYamlText, OutScene);
        }
    }
}
