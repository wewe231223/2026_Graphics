#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "Asset/AssetBinaryWriter.h"
#include "Asset/AssimpAssetImporter.h"
#include "Asset/MaterialGroupJsonSerializer.h"
#include "Asset/ModelResult.h"
#include "Utility/StdOutput.h"
#include "Utility/StringUtils.h"

#pragma comment(lib, "Asset.lib")

namespace {
    struct Options final {
    public:
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

    std::string ToLowercase(std::string Value) {
        std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char Character) {
            return static_cast<char>(std::tolower(Character));
        });
        return Value;
    }

    std::filesystem::path GetAssetFilePath(const std::filesystem::path& ProjectDirectoryPath, const std::string& InputFileName) {
        return ProjectDirectoryPath / "Asset" / InputFileName;
    }

    std::filesystem::path GetBinaryOutputPath(const std::filesystem::path& BinDirectoryPath, const std::filesystem::path& AssetFilePath) {
        return BinDirectoryPath / (AssetFilePath.stem().string() + ".bin");
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

    bool ParseOptions(int ArgumentCount, char** Arguments, Options& OutOptions) {
        if (ArgumentCount < 2) {
            return false;
        }

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
}

int main(int ArgumentCount, char** Arguments) {
    try {
        Options ParsedOptions{};
        if (ParseOptions(ArgumentCount, Arguments, ParsedOptions) == false) {
            StdOutput::PrintErrorLine("[AssetZIP] Usage: AssetZIP <AssetFileName(.fbx|.gltf|.glb)> [--flip-uv=true|false]");
            return 1;
        }

        const std::filesystem::path ProjectDirectoryPath{ std::filesystem::current_path() };
        const std::filesystem::path BinDirectoryPath{ ProjectDirectoryPath / "Bin" };
        const std::filesystem::path AssetFilePath{ GetAssetFilePath(ProjectDirectoryPath, ParsedOptions.InputFileName) };
        const std::filesystem::path BinaryOutputPath{ GetBinaryOutputPath(BinDirectoryPath, AssetFilePath) };
        const std::filesystem::path MaterialOutputPath{ GetMaterialOutputPath(BinDirectoryPath, AssetFilePath) };

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

        const ModelSummary ModelDataSummary{ BuildModelSummary(ModelData, AssetFilePath, BinaryOutputPath, MaterialOutputPath) };
        const MaterialSummary MaterialDataSummary{ BuildMaterialSummary(MaterialGroups) };

        StdOutput::PrintLine("[AssetZIP] Input file: {}", AssetFilePath.string());
        StdOutput::PrintLine("[AssetZIP] Output binary: {}", BinaryOutputPath.string());
        StdOutput::PrintLine("[AssetZIP] Output material JSON: {}", MaterialOutputPath.string());
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
