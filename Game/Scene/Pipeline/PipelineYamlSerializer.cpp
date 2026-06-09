#include "PipelineYamlSerializer.h"
#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <ryml.hpp>
#include <ryml_std.hpp>

namespace Game {
    namespace Pipeline {
        namespace {
            std::string ToString(c4::csubstr Text);
            std::string TrimCopy(const std::string& Text);
            std::string ToLowerCopy(const std::string& Text);
            bool IsPlainYamlText(const std::string& Text);
            std::string ToYamlText(const std::string& Text);
            void AppendLine(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& Text);
            bool TryReadSystemName(c4::yml::ConstNodeRef SystemNode, std::string& OutSystemName);
            PipelineYamlLoadResult ReadPipelineDefinitions(const std::string& YamlText, std::vector<PipelineDefinition>& OutPipelineDefinitions);
            bool ValidatePipelineDefinitions(const std::vector<PipelineDefinition>& PipelineDefinitions, PipelineYamlSaveResult& SaveResult);

            std::string ToString(c4::csubstr Text) {
                if (Text.str == nullptr || Text.len == 0) {
                    return {};
                }

                return std::string{ Text.str, Text.len };
            }

            std::string TrimCopy(const std::string& Text) {
                const std::string::const_iterator BeginIter{ std::find_if(Text.begin(), Text.end(), [](unsigned char Ch) { return std::isspace(Ch) == 0; }) };
                const std::string::const_reverse_iterator EndIter{ std::find_if(Text.rbegin(), Text.rend(), [](unsigned char Ch) { return std::isspace(Ch) == 0; }) };
                if (BeginIter == Text.end()) {
                    return {};
                }

                return std::string{ BeginIter, EndIter.base() };
            }

            std::string ToLowerCopy(const std::string& Text) {
                std::string LowerText{ Text };
                std::transform(LowerText.begin(), LowerText.end(), LowerText.begin(), [](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
                return LowerText;
            }

            bool IsPlainYamlText(const std::string& Text) {
                if (Text.empty() == true) {
                    return false;
                }

                const std::string LowerText{ ToLowerCopy(Text) };
                if (LowerText == "true" || LowerText == "false" || LowerText == "null" || LowerText == "~") {
                    return false;
                }

                for (unsigned char Ch : Text) {
                    const bool IsAllowedCharacter{ std::isalnum(Ch) != 0 || Ch == '_' || Ch == '-' || Ch == '.' || Ch == '/' };
                    if (IsAllowedCharacter == false) {
                        return false;
                    }
                }

                return true;
            }

            std::string ToYamlText(const std::string& Text) {
                if (IsPlainYamlText(Text) == true) {
                    return Text;
                }

                std::string EscapedText{};
                EscapedText.reserve(Text.size() + 2);
                EscapedText.push_back('"');
                for (char Ch : Text) {
                    if (Ch == '\\' || Ch == '"') {
                        EscapedText.push_back('\\');
                    }

                    EscapedText.push_back(Ch);
                }

                EscapedText.push_back('"');
                return EscapedText;
            }

            void AppendLine(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& Text) {
                for (std::size_t IndentIndex{ 0U }; IndentIndex < IndentLevel; ++IndentIndex) {
                    Stream << "  ";
                }

                Stream << Text << '\n';
            }

            bool TryReadSystemName(c4::yml::ConstNodeRef SystemNode, std::string& OutSystemName) {
                std::string SystemName{};
                if (SystemNode.is_val() == true || SystemNode.is_keyval() == true) {
                    SystemNode >> SystemName;
                    OutSystemName = TrimCopy(SystemName);
                    return OutSystemName.empty() == false;
                }

                if (SystemNode.is_map() == true) {
                    if (SystemNode.has_child("Type") == true) {
                        SystemNode["Type"] >> SystemName;
                        OutSystemName = TrimCopy(SystemName);
                        return OutSystemName.empty() == false;
                    }

                    if (SystemNode.has_child("Name") == true) {
                        SystemNode["Name"] >> SystemName;
                        OutSystemName = TrimCopy(SystemName);
                        return OutSystemName.empty() == false;
                    }
                }

                return false;
            }

            PipelineYamlLoadResult ReadPipelineDefinitions(const std::string& YamlText, std::vector<PipelineDefinition>& OutPipelineDefinitions) {
                PipelineYamlLoadResult LoadResult{};
                std::vector<PipelineDefinition> PipelineDefinitions{};

                c4::yml::Tree Tree{};
                try {
                    Tree = c4::yml::parse_in_arena(c4::to_csubstr(YamlText));
                    Tree.resolve();
                }
                catch (const std::exception& Exception) {
                    LoadResult.IsSuccess = false;
                    LoadResult.UndecidedItems.push_back(std::string{ "Pipeline YAML parse failed: " } + Exception.what());
                    return LoadResult;
                }
                catch (...) {
                    LoadResult.IsSuccess = false;
                    LoadResult.UndecidedItems.push_back("Pipeline YAML parse failed.");
                    return LoadResult;
                }

                const c4::yml::ConstNodeRef RootNode{ Tree.rootref() };
                if (RootNode.has_child("Pipelines") == false) {
                    LoadResult.IsSuccess = false;
                    LoadResult.UndecidedItems.push_back("Pipelines root is missing.");
                    return LoadResult;
                }

                const c4::yml::ConstNodeRef PipelinesNode{ RootNode["Pipelines"] };
                if (PipelinesNode.is_map() == false) {
                    LoadResult.IsSuccess = false;
                    LoadResult.UndecidedItems.push_back("Pipelines root must be a map.");
                    return LoadResult;
                }

                std::unordered_set<std::string> PipelineNames{};
                for (const c4::yml::ConstNodeRef PipelineNode : PipelinesNode.children()) {
                    const std::string PipelineName{ TrimCopy(ToString(PipelineNode.key())) };
                    if (PipelineName.empty() == true) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back("Pipeline name is empty.");
                        continue;
                    }

                    const bool IsInserted{ PipelineNames.insert(PipelineName).second };
                    if (IsInserted == false) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back(std::string{ "Duplicate Pipeline name: " } + PipelineName);
                        continue;
                    }

                    if (PipelineNode.is_map() == false || PipelineNode.has_child("Systems") == false) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back(std::string{ "Systems is missing: " } + PipelineName);
                        continue;
                    }

                    const c4::yml::ConstNodeRef SystemsNode{ PipelineNode["Systems"] };
                    if (SystemsNode.is_seq() == false || SystemsNode.num_children() == 0) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back(std::string{ "Systems is empty: " } + PipelineName);
                        continue;
                    }

                    PipelineDefinition PipelineDefinitionValue{};
                    PipelineDefinitionValue.GetName() = PipelineName;
                    bool IsPipelineDefinitionValid{ true };

                    for (const c4::yml::ConstNodeRef SystemNode : SystemsNode.children()) {
                        std::string SystemName{};
                        if (TryReadSystemName(SystemNode, SystemName) == false) {
                            LoadResult.IsSuccess = false;
                            LoadResult.UndecidedItems.push_back(std::string{ "System name is empty: " } + PipelineName);
                            IsPipelineDefinitionValid = false;
                            continue;
                        }

                        PipelineDefinitionValue.GetSystemNames().push_back(SystemName);
                    }

                    if (IsPipelineDefinitionValid == false || PipelineDefinitionValue.GetSystemNames().empty() == true) {
                        continue;
                    }

                    if (PipelineDefinitions.size() >= static_cast<std::size_t>(InvalidPipelineId)) {
                        LoadResult.IsSuccess = false;
                        LoadResult.UndecidedItems.push_back("Pipeline count exceeds PipelineId range.");
                        continue;
                    }

                    PipelineDefinitionValue.SetPipelineId(static_cast<PipelineId>(PipelineDefinitions.size()));
                    PipelineDefinitions.push_back(std::move(PipelineDefinitionValue));
                }

                if (LoadResult.IsSuccess == true) {
                    OutPipelineDefinitions = std::move(PipelineDefinitions);
                }

                return LoadResult;
            }

            bool ValidatePipelineDefinitions(const std::vector<PipelineDefinition>& PipelineDefinitions, PipelineYamlSaveResult& SaveResult) {
                std::unordered_set<std::string> PipelineNames{};
                for (const PipelineDefinition& PipelineDefinitionValue : PipelineDefinitions) {
                    const std::string PipelineName{ TrimCopy(PipelineDefinitionValue.GetName()) };
                    if (PipelineName.empty() == true) {
                        SaveResult.IsSuccess = false;
                        SaveResult.UndecidedItems.push_back("Pipeline name is empty.");
                        continue;
                    }

                    const bool IsInserted{ PipelineNames.insert(PipelineName).second };
                    if (IsInserted == false) {
                        SaveResult.IsSuccess = false;
                        SaveResult.UndecidedItems.push_back(std::string{ "Duplicate Pipeline name: " } + PipelineName);
                    }

                    if (PipelineDefinitionValue.GetSystemNames().empty() == true) {
                        SaveResult.IsSuccess = false;
                        SaveResult.UndecidedItems.push_back(std::string{ "Systems is empty: " } + PipelineName);
                        continue;
                    }

                    for (const std::string& SystemNameValue : PipelineDefinitionValue.GetSystemNames()) {
                        const std::string SystemName{ TrimCopy(SystemNameValue) };
                        if (SystemName.empty() == true) {
                            SaveResult.IsSuccess = false;
                            SaveResult.UndecidedItems.push_back(std::string{ "System name is empty: " } + PipelineName);
                        }
                    }
                }

                return SaveResult.IsSuccess;
            }
        }

        PipelineYamlSaveResult::PipelineYamlSaveResult() = default;
        PipelineYamlSaveResult::~PipelineYamlSaveResult() = default;
        PipelineYamlSaveResult::PipelineYamlSaveResult(const PipelineYamlSaveResult& Other) = default;
        PipelineYamlSaveResult& PipelineYamlSaveResult::operator=(const PipelineYamlSaveResult& Other) = default;
        PipelineYamlSaveResult::PipelineYamlSaveResult(PipelineYamlSaveResult&& Other) noexcept = default;
        PipelineYamlSaveResult& PipelineYamlSaveResult::operator=(PipelineYamlSaveResult&& Other) noexcept = default;

        PipelineYamlLoadResult::PipelineYamlLoadResult() = default;
        PipelineYamlLoadResult::~PipelineYamlLoadResult() = default;
        PipelineYamlLoadResult::PipelineYamlLoadResult(const PipelineYamlLoadResult& Other) = default;
        PipelineYamlLoadResult& PipelineYamlLoadResult::operator=(const PipelineYamlLoadResult& Other) = default;
        PipelineYamlLoadResult::PipelineYamlLoadResult(PipelineYamlLoadResult&& Other) noexcept = default;
        PipelineYamlLoadResult& PipelineYamlLoadResult::operator=(PipelineYamlLoadResult&& Other) noexcept = default;

        PipelineYamlSerializer::PipelineYamlSerializer() = default;
        PipelineYamlSerializer::~PipelineYamlSerializer() = default;
        PipelineYamlSerializer::PipelineYamlSerializer(const PipelineYamlSerializer& Other) = default;
        PipelineYamlSerializer& PipelineYamlSerializer::operator=(const PipelineYamlSerializer& Other) = default;
        PipelineYamlSerializer::PipelineYamlSerializer(PipelineYamlSerializer&& Other) noexcept = default;
        PipelineYamlSerializer& PipelineYamlSerializer::operator=(PipelineYamlSerializer&& Other) noexcept = default;

        PipelineYamlLoadResult PipelineYamlSerializer::Deserialize(const std::string& YamlText, std::vector<PipelineDefinition>& OutPipelineDefinitions) const {
            return ReadPipelineDefinitions(YamlText, OutPipelineDefinitions);
        }

        PipelineYamlLoadResult PipelineYamlSerializer::Deserialize(const std::string& YamlText, Scene& OutScene) const {
            std::vector<PipelineDefinition> PipelineDefinitions{};
            PipelineYamlLoadResult LoadResult{ ReadPipelineDefinitions(YamlText, PipelineDefinitions) };
            if (LoadResult.IsSuccess == false) {
                return LoadResult;
            }

            OutScene.ClearPipelineDefinitions();
            for (PipelineDefinition& PipelineDefinitionValue : PipelineDefinitions) {
                if (OutScene.AddPipelineDefinition(std::move(PipelineDefinitionValue)) == false) {
                    LoadResult.IsSuccess = false;
                    LoadResult.UndecidedItems.push_back("Pipeline definition could not be added to Scene.");
                    break;
                }
            }

            return LoadResult;
        }

        PipelineYamlLoadResult PipelineYamlSerializer::DeserializeFromFile(const std::string& YamlFilePath, Scene& OutScene) const {
            std::ifstream InputStream{ YamlFilePath, std::ios::in | std::ios::binary };
            PipelineYamlLoadResult LoadResult{};
            if (InputStream.is_open() == false) {
                LoadResult.IsSuccess = false;
                LoadResult.UndecidedItems.push_back(std::string{ "Pipeline YAML file could not be opened: " } + YamlFilePath);
                return LoadResult;
            }

            std::stringstream Buffer{};
            Buffer << InputStream.rdbuf();
            return Deserialize(Buffer.str(), OutScene);
        }

        PipelineYamlSaveResult PipelineYamlSerializer::Serialize(const std::vector<PipelineDefinition>& PipelineDefinitions, std::string& OutYamlText) const {
            PipelineYamlSaveResult SaveResult{};
            if (ValidatePipelineDefinitions(PipelineDefinitions, SaveResult) == false) {
                OutYamlText.clear();
                return SaveResult;
            }

            std::ostringstream Stream{};
            if (PipelineDefinitions.empty() == true) {
                AppendLine(Stream, 0U, "Pipelines: {}");
                OutYamlText = Stream.str();
                return SaveResult;
            }

            AppendLine(Stream, 0U, "Pipelines:");
            for (const PipelineDefinition& PipelineDefinitionValue : PipelineDefinitions) {
                const std::string PipelineName{ TrimCopy(PipelineDefinitionValue.GetName()) };
                AppendLine(Stream, 1U, std::string{ ToYamlText(PipelineName) } + ":");
                AppendLine(Stream, 2U, "Systems:");

                for (const std::string& SystemNameValue : PipelineDefinitionValue.GetSystemNames()) {
                    const std::string SystemName{ TrimCopy(SystemNameValue) };
                    AppendLine(Stream, 3U, std::string{ "- " } + ToYamlText(SystemName));
                }

                AppendLine(Stream, 0U, "");
            }

            OutYamlText = Stream.str();
            return SaveResult;
        }

        PipelineYamlSaveResult PipelineYamlSerializer::Serialize(const Scene& TargetScene, std::string& OutYamlText) const {
            return Serialize(TargetScene.GetPipelineDefinitions(), OutYamlText);
        }

        PipelineYamlSaveResult PipelineYamlSerializer::SerializeToFile(const Scene& TargetScene, const std::string& YamlFilePath) const {
            std::string YamlText{};
            PipelineYamlSaveResult SaveResult{ Serialize(TargetScene, YamlText) };
            if (SaveResult.IsSuccess == false) {
                return SaveResult;
            }

            std::ofstream OutputStream{ YamlFilePath, std::ios::out | std::ios::binary | std::ios::trunc };
            if (OutputStream.is_open() == false) {
                SaveResult.IsSuccess = false;
                SaveResult.UndecidedItems.push_back(std::string{ "Pipeline YAML file could not be written: " } + YamlFilePath);
                return SaveResult;
            }

            OutputStream << YamlText;
            if (OutputStream.good() == false) {
                SaveResult.IsSuccess = false;
                SaveResult.UndecidedItems.push_back(std::string{ "Pipeline YAML file write failed: " } + YamlFilePath);
            }

            return SaveResult;
        }
    }
}
