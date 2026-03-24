#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "Asset/AnimationClipBinaryWriter.h"
#include "Asset/AnimationClipResult.h"
#include "Asset/AssetBinaryWriter.h"
#include "Asset/AssimpAnimationClipImporter.h"
#include "Asset/AssimpAssetImporter.h"
#include "Asset/MaterialGroupJsonSerializer.h"
#include "Asset/ModelResult.h"
#include "Utility/StdOutput.h"
#include "Utility/StringUtils.h"

#pragma comment(lib, "Asset.lib")

namespace {
    enum class ExportCommand : std::uint8_t {
        Model,
        AnimationClip,
        Help,
    };

    struct Options final {
    public:
        ExportCommand Command{ ExportCommand::Model };
        std::string InputFileName{};
        bool IsUvFlipEnabled{ false };
    };

    struct MaterialSummary final {
    public:
        std::size_t GroupCount{ 0 };
        std::size_t ItemCount{ 0 };
        std::size_t PropertyCount{ 0 };
        std::size_t TexturePathPropertyCount{ 0 };
        std::size_t PbrMaterialCount{ 0 };
    };

    struct ModelSummary final {
    public:
        std::size_t NodeCount{ 0 };
        std::size_t MeshNodeCount{ 0 };
        std::size_t SkinnedMeshNodeCount{ 0 };
        std::size_t BoneInfoCount{ 0 };
        std::size_t ChildLinkCount{ 0 };
        std::size_t SubMeshCount{ 0 };
        std::size_t VertexCount{ 0 };
        std::size_t PositionCount{ 0 };
        std::size_t NormalCount{ 0 };
        std::size_t ColorCount{ 0 };
        std::size_t TangentCount{ 0 };
        std::size_t BitangentCount{ 0 };
        std::size_t BoneIndexCount{ 0 };
        std::size_t BoneWeightCount{ 0 };
        std::size_t IndexCount{ 0 };
        std::array<std::size_t, 4> TexCoordCounts{};
        std::uintmax_t InputFileSizeInBytes{ 0 };
        std::uintmax_t OutputBinarySizeInBytes{ 0 };
        std::uintmax_t OutputMaterialJsonSizeInBytes{ 0 };
    };

    struct AnimationSummary final {
    public:
        std::size_t ClipCount{ 0 };
        std::size_t ChannelCount{ 0 };
        std::size_t PositionKeyCount{ 0 };
        std::size_t RotationKeyCount{ 0 };
        std::size_t ScaleKeyCount{ 0 };
        std::uintmax_t InputFileSizeInBytes{ 0 };
        std::uintmax_t OutputBinarySizeInBytes{ 0 };
    };

    struct AnimationClipSummary final {
    public:
        std::string Name{};
        double Duration{ 0.0 };
        double TicksPerSecond{ 0.0 };
        std::size_t ChannelCount{ 0 };
        std::size_t PositionKeyCount{ 0 };
        std::size_t RotationKeyCount{ 0 };
        std::size_t ScaleKeyCount{ 0 };
    };

    struct NodeSummary final {
    public:
        std::uint32_t Id{ 0 };
        std::string Name{};
        std::string Path{};
        std::string SkinBoneRootNodeName{};
        std::size_t Depth{ 0 };
        std::size_t ChildCount{ 0 };
        std::size_t SubMeshCount{ 0 };
        std::size_t VertexCount{ 0 };
        std::size_t IndexCount{ 0 };
        std::size_t TriangleCount{ 0 };
        std::size_t BoneInfoCount{ 0 };
        bool IsMeshNode{ false };
        bool IsSkinnedMesh{ false };
    };

    std::string ToLowercase(std::string Value) {
        std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char Character) {
            return static_cast<char>(std::tolower(Character));
        });
        return Value;
    }

    bool TryParseCommand(const std::string& Value, ExportCommand& OutCommand) {
        const std::string LowercasedValue{ ToLowercase(Value) };
        if (LowercasedValue == "model") {
            OutCommand = ExportCommand::Model;
            return true;
        }

        if (LowercasedValue == "anim" || LowercasedValue == "animation" || LowercasedValue == "animationclip") {
            OutCommand = ExportCommand::AnimationClip;
            return true;
        }

        if (LowercasedValue == "help" || LowercasedValue == "--help" || LowercasedValue == "-h") {
            OutCommand = ExportCommand::Help;
            return true;
        }

        return false;
    }

    void PrintHelp() {
        StdOutput::PrintLine("[AssetZIP] Usage");
        StdOutput::PrintLine("[AssetZIP]   AssetZIP help");
        StdOutput::PrintLine("[AssetZIP]   AssetZIP model <AssetFileName(.fbx|.gltf|.glb)> [--flip-uv=true|false]");
        StdOutput::PrintLine("[AssetZIP]   AssetZIP <AssetFileName(.fbx|.gltf|.glb)> [--flip-uv=true|false]");
        StdOutput::PrintLine("[AssetZIP]   AssetZIP animation <AssetFileName(.fbx|.gltf|.glb)>");
        StdOutput::PrintLine("[AssetZIP]");
        StdOutput::PrintLine("[AssetZIP] Commands");
        StdOutput::PrintLine("[AssetZIP]   help       : Display available commands and options.");
        StdOutput::PrintLine("[AssetZIP]   model      : Generate model binary (.bin) and material JSON.");
        StdOutput::PrintLine("[AssetZIP]   animation  : Generate animation clip binary (.animbin).");
        StdOutput::PrintLine("[AssetZIP]");
        StdOutput::PrintLine("[AssetZIP] Options");
        StdOutput::PrintLine("[AssetZIP]   --flip-uv=true|false  : Set whether to flip the UV Y-axis (model command only).");
    }

    std::filesystem::path GetAssetFilePath(const std::filesystem::path& ProjectDirectoryPath, const std::string& InputFileName) {
        return ProjectDirectoryPath / "Asset" / InputFileName;
    }

    std::filesystem::path GetBinaryOutputPath(const std::filesystem::path& BinDirectoryPath, const std::filesystem::path& AssetFilePath) {
        return BinDirectoryPath / (AssetFilePath.stem().string() + ".bin");
    }

    std::filesystem::path GetAnimationBinaryOutputPath(const std::filesystem::path& BinDirectoryPath, const std::filesystem::path& AssetFilePath) {
        return BinDirectoryPath / (AssetFilePath.stem().string() + ".animbin");
    }

    std::filesystem::path GetMaterialOutputPath(const std::filesystem::path& BinDirectoryPath, const std::filesystem::path& AssetFilePath) {
        return BinDirectoryPath / (AssetFilePath.stem().string() + "_materials.json");
    }

    bool IsSupportedAssetExtension(const std::filesystem::path& AssetFilePath) {
        const std::string Extension{ ToLowercase(AssetFilePath.extension().string()) };
        return Extension == ".fbx" || Extension == ".gltf" || Extension == ".glb";
    }

    void LoadModelFromAssetFile(const std::filesystem::path& AssetFilePath, asset::ModelResult& OutModelData, std::vector<asset::MaterialGroup>& OutMaterialGroups, bool IsUvFlipEnabled) {
        asset::AssimpAssetImporter Importer{ asset::GraphicsAPI::DirectX };
        Importer.LoadFromFile(AssetFilePath.string(), OutModelData, OutMaterialGroups, IsUvFlipEnabled);
    }

    void LoadAnimationClipsFromAssetFile(const std::filesystem::path& AssetFilePath, asset::AnimationClipResult& OutClipData) {
        asset::AssimpAnimationClipImporter Importer{ asset::GraphicsAPI::DirectX };
        Importer.LoadFromFile(AssetFilePath.string(), OutClipData);
    }

    bool ParseOptions(int ArgumentCount, char** Arguments, Options& OutOptions) {
        if (ArgumentCount < 2) {
            return false;
        }

        ExportCommand ParsedCommand{ ExportCommand::Model };
        if (TryParseCommand(Arguments[1], ParsedCommand)) {
            OutOptions.Command = ParsedCommand;
            if (ParsedCommand == ExportCommand::Help) {
                return true;
            }

            if (ArgumentCount < 3) {
                return false;
            }

            OutOptions.InputFileName = Arguments[2];
            for (int ArgumentIndex{ 3 }; ArgumentIndex < ArgumentCount; ++ArgumentIndex) {
                const std::string Argument{ Arguments[ArgumentIndex] };
                if (Argument.rfind("--flip-uv=", 0) == 0) {
                    OutOptions.IsUvFlipEnabled = ToLowercase(Argument.substr(10)) == "true";
                }
            }
            return true;
        }

        OutOptions.Command = ExportCommand::Model;
        OutOptions.InputFileName = Arguments[1];
        for (int ArgumentIndex{ 2 }; ArgumentIndex < ArgumentCount; ++ArgumentIndex) {
            const std::string Argument{ Arguments[ArgumentIndex] };
            if (Argument.rfind("--flip-uv=", 0) == 0) {
                OutOptions.IsUvFlipEnabled = ToLowercase(Argument.substr(10)) == "true";
            }
        }

        return true;
    }

    std::uintmax_t GetFileSizeOrZero(const std::filesystem::path& FilePath) {
        std::error_code ErrorCode{};
        const std::uintmax_t FileSize{ std::filesystem::file_size(FilePath, ErrorCode) };
        if (ErrorCode) {
            return 0;
        }

        return FileSize;
    }

    MaterialSummary BuildMaterialSummary(const std::vector<asset::MaterialGroup>& MaterialGroups) {
        MaterialSummary Summary{};
        Summary.GroupCount = MaterialGroups.size();

        for (const asset::MaterialGroup& MaterialGroup : MaterialGroups) {
            Summary.ItemCount += MaterialGroup.Items.size();

            for (const asset::MaterialGroupItem& MaterialGroupItem : MaterialGroup.Items) {
                if (MaterialGroupItem.MaterialData.PBR) {
                    ++Summary.PbrMaterialCount;
                }

                Summary.PropertyCount += MaterialGroupItem.MaterialData.Properties.size();
                for (const asset::MaterialProperty& Property : MaterialGroupItem.MaterialData.Properties) {
                    if (Property.Data.GetKind() == asset::MaterialMapKind::String && Property.Data.GetString().empty() == false) {
                        ++Summary.TexturePathPropertyCount;
                    }
                }
            }
        }

        return Summary;
    }

    std::string BuildNodePath(const asset::ModelNode& Node) {
        const std::vector<const asset::ModelNode*> Chain{ Node.GetChildChain() };
        std::string Path{};

        for (std::size_t Index{ 0 }; Index < Chain.size(); ++Index) {
            if (Index > 0) {
                Path += " / ";
            }

            const std::string& Name{ Chain[Index]->GetName() };
            if (Name.empty()) {
                Path += "<Unnamed>";
            }
            else {
                Path += Name;
            }
        }

        return Path;
    }

    NodeSummary BuildNodeSummary(const asset::ModelNode& Node) {
        const asset::VertexAttributes& Vertices{ Node.Vertices() };
        const std::vector<std::uint32_t>& Indices{ Node.Indices() };
        const std::vector<asset::ModelNode::SubMesh>& SubMeshes{ Node.GetSubMeshes() };
        const std::vector<asset::ModelBoneInfo>& BoneInfos{ Node.BoneInfos() };
        const std::vector<const asset::ModelNode*> NodeChain{ Node.GetChildChain() };
        NodeSummary Summary{};
        Summary.Id = Node.GetId();
        Summary.Name = Node.GetName().empty() ? "<Unnamed>" : Node.GetName();
        Summary.Path = BuildNodePath(Node);
        Summary.SkinBoneRootNodeName = Node.GetSkinBoneRootNodeName();
        Summary.Depth = NodeChain.empty() ? 0 : NodeChain.size() - 1;
        Summary.ChildCount = Node.GetChildren().size();
        Summary.SubMeshCount = SubMeshes.size();
        Summary.VertexCount = Vertices.VertexCount();
        Summary.IndexCount = Indices.size();
        Summary.TriangleCount = Indices.size() / 3;
        Summary.BoneInfoCount = BoneInfos.size();
        Summary.IsMeshNode = SubMeshes.empty() == false;
        Summary.IsSkinnedMesh = Node.IsSkinnedMesh();
        return Summary;
    }

    std::vector<NodeSummary> BuildNodeSummaries(const asset::ModelResult& ModelData) {
        std::vector<NodeSummary> Summaries{};
        Summaries.reserve(ModelData.NodeCount());

        for (const std::unique_ptr<asset::ModelNode>& NodePointer : ModelData.Nodes()) {
            Summaries.push_back(BuildNodeSummary(*NodePointer));
        }

        return Summaries;
    }

    ModelSummary BuildModelSummary(const asset::ModelResult& ModelData, const std::filesystem::path& AssetFilePath, const std::filesystem::path& BinaryOutputPath, const std::filesystem::path& MaterialOutputPath) {
        ModelSummary Summary{};
        Summary.NodeCount = ModelData.NodeCount();
        Summary.InputFileSizeInBytes = GetFileSizeOrZero(AssetFilePath);
        Summary.OutputBinarySizeInBytes = GetFileSizeOrZero(BinaryOutputPath);
        Summary.OutputMaterialJsonSizeInBytes = GetFileSizeOrZero(MaterialOutputPath);

        for (const std::unique_ptr<asset::ModelNode>& NodePointer : ModelData.Nodes()) {
            const asset::ModelNode& Node{ *NodePointer };
            const asset::VertexAttributes& Vertices{ Node.Vertices() };
            const std::vector<std::uint32_t>& Indices{ Node.Indices() };
            const std::vector<asset::ModelNode::SubMesh>& SubMeshes{ Node.GetSubMeshes() };
            const std::vector<asset::ModelBoneInfo>& BoneInfos{ Node.BoneInfos() };

            Summary.ChildLinkCount += Node.GetChildren().size();
            Summary.SubMeshCount += SubMeshes.size();
            Summary.BoneInfoCount += BoneInfos.size();
            Summary.VertexCount += Vertices.VertexCount();
            Summary.PositionCount += Vertices.Positions.size();
            Summary.NormalCount += Vertices.Normals.size();
            Summary.ColorCount += Vertices.Colors.size();
            Summary.TangentCount += Vertices.Tangents.size();
            Summary.BitangentCount += Vertices.Bitangents.size();
            Summary.BoneIndexCount += Vertices.BoneIndices.size();
            Summary.BoneWeightCount += Vertices.BoneWeights.size();
            Summary.IndexCount += Indices.size();

            for (std::size_t TexCoordIndex{ 0 }; TexCoordIndex < Vertices.TexCoords.size(); ++TexCoordIndex) {
                Summary.TexCoordCounts[TexCoordIndex] += Vertices.TexCoords[TexCoordIndex].size();
            }

            if (SubMeshes.empty() == false) {
                ++Summary.MeshNodeCount;
            }

            if (Node.IsSkinnedMesh()) {
                ++Summary.SkinnedMeshNodeCount;
            }
        }

        return Summary;
    }

    AnimationSummary BuildAnimationSummary(const asset::AnimationClipResult& ClipData, const std::filesystem::path& AssetFilePath, const std::filesystem::path& AnimationBinaryOutputPath) {
        AnimationSummary Summary{};
        Summary.ClipCount = ClipData.ClipCount();
        Summary.InputFileSizeInBytes = GetFileSizeOrZero(AssetFilePath);
        Summary.OutputBinarySizeInBytes = GetFileSizeOrZero(AnimationBinaryOutputPath);

        for (const asset::AnimationClip& Clip : ClipData.Clips()) {
            Summary.ChannelCount += Clip.Channels.size();
            for (const asset::AnimationChannel& Channel : Clip.Channels) {
                Summary.PositionKeyCount += Channel.PositionKeys.size();
                Summary.RotationKeyCount += Channel.RotationKeys.size();
                Summary.ScaleKeyCount += Channel.ScaleKeys.size();
            }
        }

        return Summary;
    }

    AnimationClipSummary BuildAnimationClipSummary(const asset::AnimationClip& Clip) {
        AnimationClipSummary Summary{};
        Summary.Name = Clip.Name.empty() ? "<Unnamed>" : Clip.Name;
        Summary.Duration = Clip.Duration;
        Summary.TicksPerSecond = Clip.TicksPerSecond;
        Summary.ChannelCount = Clip.Channels.size();

        for (const asset::AnimationChannel& Channel : Clip.Channels) {
            Summary.PositionKeyCount += Channel.PositionKeys.size();
            Summary.RotationKeyCount += Channel.RotationKeys.size();
            Summary.ScaleKeyCount += Channel.ScaleKeys.size();
        }

        return Summary;
    }

    std::vector<AnimationClipSummary> BuildAnimationClipSummaries(const asset::AnimationClipResult& ClipData) {
        std::vector<AnimationClipSummary> Summaries{};
        Summaries.reserve(ClipData.ClipCount());

        for (const asset::AnimationClip& Clip : ClipData.Clips()) {
            Summaries.push_back(BuildAnimationClipSummary(Clip));
        }

        return Summaries;
    }

    void PrintNodeSummary(const NodeSummary& Summary) {
        StdOutput::PrintLine("[AssetZIP] Node #{}: {}", Summary.Id, Summary.Name);
        StdOutput::PrintLine("[AssetZIP]   Path: {}", Summary.Path);
        StdOutput::PrintLine("[AssetZIP]   Depth: {}, Children: {}, Mesh: {}, Skinned: {}", Summary.Depth, Summary.ChildCount, Summary.IsMeshNode ? "true" : "false", Summary.IsSkinnedMesh ? "true" : "false");
        StdOutput::PrintLine("[AssetZIP]   SkinBoneRootNodeName: {}", Summary.SkinBoneRootNodeName.empty() ? "<None>" : Summary.SkinBoneRootNodeName);
        StdOutput::PrintLine("[AssetZIP]   SubMeshes: {}, Vertices: {}, Indices: {}, Triangles: {}, Bones: {}", Summary.SubMeshCount, Summary.VertexCount, Summary.IndexCount, Summary.TriangleCount, Summary.BoneInfoCount);
    }

    void PrintNodeSummaries(const std::vector<NodeSummary>& Summaries) {
        StdOutput::PrintLine("[AssetZIP] Node processing results begin");

        for (const NodeSummary& Summary : Summaries) {
            PrintNodeSummary(Summary);
            StdOutput::PrintLine("[AssetZIP]");
        }

        StdOutput::PrintLine("[AssetZIP] Node processing results end");
    }

    void PrintModelSummary(const ModelSummary& Summary) {
        StdOutput::PrintLine("[AssetZIP] Node count: {}", Summary.NodeCount);
        StdOutput::PrintLine("[AssetZIP] Mesh node count: {}", Summary.MeshNodeCount);
        StdOutput::PrintLine("[AssetZIP] Skinned mesh node count: {}", Summary.SkinnedMeshNodeCount);
        StdOutput::PrintLine("[AssetZIP] Child link count: {}", Summary.ChildLinkCount);
        StdOutput::PrintLine("[AssetZIP] Submesh count: {}", Summary.SubMeshCount);
        StdOutput::PrintLine("[AssetZIP] Vertex count: {}", Summary.VertexCount);
        StdOutput::PrintLine("[AssetZIP] Position count: {}", Summary.PositionCount);
        StdOutput::PrintLine("[AssetZIP] Normal count: {}", Summary.NormalCount);
        StdOutput::PrintLine("[AssetZIP] TexCoord0 count: {}", Summary.TexCoordCounts[0]);
        StdOutput::PrintLine("[AssetZIP] TexCoord1 count: {}", Summary.TexCoordCounts[1]);
        StdOutput::PrintLine("[AssetZIP] TexCoord2 count: {}", Summary.TexCoordCounts[2]);
        StdOutput::PrintLine("[AssetZIP] TexCoord3 count: {}", Summary.TexCoordCounts[3]);
        StdOutput::PrintLine("[AssetZIP] Color count: {}", Summary.ColorCount);
        StdOutput::PrintLine("[AssetZIP] Tangent count: {}", Summary.TangentCount);
        StdOutput::PrintLine("[AssetZIP] Bitangent count: {}", Summary.BitangentCount);
        StdOutput::PrintLine("[AssetZIP] Bone index count: {}", Summary.BoneIndexCount);
        StdOutput::PrintLine("[AssetZIP] Bone weight count: {}", Summary.BoneWeightCount);
        StdOutput::PrintLine("[AssetZIP] Bone info count: {}", Summary.BoneInfoCount);
        StdOutput::PrintLine("[AssetZIP] Index count: {}", Summary.IndexCount);
        StdOutput::PrintLine("[AssetZIP] Triangle count: {}", Summary.IndexCount / 3);
        StdOutput::PrintLine("[AssetZIP] Input file size(bytes): {}", Summary.InputFileSizeInBytes);
        StdOutput::PrintLine("[AssetZIP] Output binary size(bytes): {}", Summary.OutputBinarySizeInBytes);
        StdOutput::PrintLine("[AssetZIP] Output material JSON size(bytes): {}", Summary.OutputMaterialJsonSizeInBytes);
    }

    void PrintMaterialSummary(const MaterialSummary& Summary) {
        StdOutput::PrintLine("[AssetZIP] Material group count: {}", Summary.GroupCount);
        StdOutput::PrintLine("[AssetZIP] Material item count: {}", Summary.ItemCount);
        StdOutput::PrintLine("[AssetZIP] Material property count: {}", Summary.PropertyCount);
        StdOutput::PrintLine("[AssetZIP] Texture path property count: {}", Summary.TexturePathPropertyCount);
        StdOutput::PrintLine("[AssetZIP] PBR material count: {}", Summary.PbrMaterialCount);
    }

    void PrintAnimationClipSummary(const AnimationClipSummary& Summary, std::size_t ClipIndex) {
        StdOutput::PrintLine("[AssetZIP] Animation clip #{}: {}", ClipIndex, Summary.Name);
        StdOutput::PrintLine("[AssetZIP]   Duration: {}, TicksPerSecond: {}", Summary.Duration, Summary.TicksPerSecond);
        StdOutput::PrintLine("[AssetZIP]   Channels: {}, PositionKeys: {}, RotationKeys: {}, ScaleKeys: {}", Summary.ChannelCount, Summary.PositionKeyCount, Summary.RotationKeyCount, Summary.ScaleKeyCount);
    }

    void PrintAnimationClipSummaries(const std::vector<AnimationClipSummary>& Summaries) {
        StdOutput::PrintLine("[AssetZIP] Animation clip processing results begin");

        for (std::size_t Index{ 0 }; Index < Summaries.size(); ++Index) {
            PrintAnimationClipSummary(Summaries[Index], Index);
            StdOutput::PrintLine("[AssetZIP]");
        }

        StdOutput::PrintLine("[AssetZIP] Animation clip processing results end");
    }

    void PrintAnimationSummary(const AnimationSummary& Summary) {
        StdOutput::PrintLine("[AssetZIP] Animation clip count: {}", Summary.ClipCount);
        StdOutput::PrintLine("[AssetZIP] Animation channel count: {}", Summary.ChannelCount);
        StdOutput::PrintLine("[AssetZIP] Animation position key count: {}", Summary.PositionKeyCount);
        StdOutput::PrintLine("[AssetZIP] Animation rotation key count: {}", Summary.RotationKeyCount);
        StdOutput::PrintLine("[AssetZIP] Animation scale key count: {}", Summary.ScaleKeyCount);
        StdOutput::PrintLine("[AssetZIP] Input file size(bytes): {}", Summary.InputFileSizeInBytes);
        StdOutput::PrintLine("[AssetZIP] Output animation binary size(bytes): {}", Summary.OutputBinarySizeInBytes);
    }
}

int main(int ArgumentCount, char** Arguments) {
    try {
        Options ParsedOptions{};
        if (ParseOptions(ArgumentCount, Arguments, ParsedOptions) == false) {
            PrintHelp();
            return 1;
        }

        if (ParsedOptions.Command == ExportCommand::Help) {
            PrintHelp();
            return 0;
        }

        const std::filesystem::path ProjectDirectoryPath{ std::filesystem::current_path() };
        const std::filesystem::path BinDirectoryPath{ ProjectDirectoryPath / "Bin" };
        const std::filesystem::path AssetFilePath{ GetAssetFilePath(ProjectDirectoryPath, ParsedOptions.InputFileName) };
        const std::filesystem::path BinaryOutputPath{ GetBinaryOutputPath(BinDirectoryPath, AssetFilePath) };
        const std::filesystem::path MaterialOutputPath{ GetMaterialOutputPath(BinDirectoryPath, AssetFilePath) };
        const std::filesystem::path AnimationBinaryOutputPath{ GetAnimationBinaryOutputPath(BinDirectoryPath, AssetFilePath) };

        if (std::filesystem::exists(AssetFilePath) == false) {
            StdOutput::PrintErrorLine("[AssetZIP] Input asset file was not found: {}", AssetFilePath.string());
            return 1;
        }

        if (IsSupportedAssetExtension(AssetFilePath) == false) {
            StdOutput::PrintErrorLine("[AssetZIP] Supported input formats are .fbx, .gltf, .glb: {}", AssetFilePath.string());
            return 1;
        }

        std::error_code ErrorCode{};
        std::filesystem::create_directories(BinDirectoryPath, ErrorCode);
        if (ErrorCode) {
            StdOutput::PrintErrorLine("[AssetZIP] Failed to create Bin directory: {}", BinDirectoryPath.string());
            return 1;
        }

        if (ParsedOptions.Command == ExportCommand::AnimationClip) {
            asset::AnimationClipResult AnimationClipData{};
            LoadAnimationClipsFromAssetFile(AssetFilePath, AnimationClipData);

            asset::AnimationClipBinaryWriter AnimationBinaryWriter{};
            if (AnimationBinaryWriter.WriteToFile(AnimationBinaryOutputPath.string(), AnimationClipData) == false) {
                StdOutput::PrintErrorLine("[AssetZIP] Failed to create animation binary file: {}", AnimationBinaryOutputPath.string());
                return 1;
            }

            const std::vector<AnimationClipSummary> AnimationClipSummaries{ BuildAnimationClipSummaries(AnimationClipData) };
            const AnimationSummary AnimationDataSummary{ BuildAnimationSummary(AnimationClipData, AssetFilePath, AnimationBinaryOutputPath) };
            StdOutput::PrintLine("[AssetZIP] Export command: animation");
            StdOutput::PrintLine("[AssetZIP] Input file: {}", AssetFilePath.string());
            StdOutput::PrintLine("[AssetZIP] Output animation binary: {}", AnimationBinaryOutputPath.string());
            PrintAnimationClipSummaries(AnimationClipSummaries);
            PrintAnimationSummary(AnimationDataSummary);
            return 0;
        }

        asset::ModelResult ModelData{};
        std::vector<asset::MaterialGroup> MaterialGroups{};
        LoadModelFromAssetFile(AssetFilePath, ModelData, MaterialGroups, ParsedOptions.IsUvFlipEnabled);

        asset::AssetBinaryWriter AssetBinaryWriterData{};
        if (AssetBinaryWriterData.WriteToFile(BinaryOutputPath.string(), ModelData) == false) {
            StdOutput::PrintErrorLine("[AssetZIP] Failed to create binary file: {}", BinaryOutputPath.string());
            return 1;
        }

        asset::MaterialGroupJsonSerializer Serializer{};
        if (Serializer.WriteToFile(MaterialOutputPath.string(), MaterialGroups) == false) {
            StdOutput::PrintErrorLine("[AssetZIP] Failed to create material JSON file: {}", MaterialOutputPath.string());
            return 1;
        }

        const std::vector<NodeSummary> NodeSummaries{ BuildNodeSummaries(ModelData) };
        const ModelSummary ModelDataSummary{ BuildModelSummary(ModelData, AssetFilePath, BinaryOutputPath, MaterialOutputPath) };
        const MaterialSummary MaterialDataSummary{ BuildMaterialSummary(MaterialGroups) };

        StdOutput::PrintLine("[AssetZIP] Export command: model");
        StdOutput::PrintLine("[AssetZIP] Input file: {}", AssetFilePath.string());
        StdOutput::PrintLine("[AssetZIP] Output binary: {}", BinaryOutputPath.string());
        StdOutput::PrintLine("[AssetZIP] Output material JSON: {}", MaterialOutputPath.string());
        PrintNodeSummaries(NodeSummaries);
        PrintModelSummary(ModelDataSummary);
        PrintMaterialSummary(MaterialDataSummary);
        StdOutput::PrintLine("[AssetZIP] UV flip enabled: {}", ParsedOptions.IsUvFlipEnabled ? "true" : "false");
        return 0;
    }
    catch (const std::exception& ExceptionData) {
        StdOutput::PrintErrorLine("[AssetZIP] {}", ExceptionData.what());
        return 1;
    }
}
