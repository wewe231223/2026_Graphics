#include <exception>
#include <filesystem>
#include <string>
#include <vector>
#include "Asset/AssetBinaryWriter.h"
#include "Asset/FbxAssetImporter.h"
#include "Asset/MaterialGroupJsonSerializer.h"
#include "Asset/ModelResult.h"
#include "Utility/StdOutput.h"

#pragma comment(lib, "Asset.lib")


namespace {

    struct RunOptions final {
    public:
        std::string InputFileName{};
        bool IsUvFlipEnabled{ true };
    };

    bool TryParseUvFlipValue(const std::string& Value, bool& OutIsUvFlipEnabled) {
        if (Value == "1" || Value == "true" || Value == "True" || Value == "TRUE" || Value == "yes" || Value == "Yes" || Value == "YES" || Value == "on" || Value == "On" || Value == "ON") {
            OutIsUvFlipEnabled = true;
            return true;
        }

        if (Value == "0" || Value == "false" || Value == "False" || Value == "FALSE" || Value == "no" || Value == "No" || Value == "NO" || Value == "off" || Value == "Off" || Value == "OFF") {
            OutIsUvFlipEnabled = false;
            return true;
        }

        return false;
    }

    bool TryParseArguments(int ArgCount, char* ArgValues[], RunOptions& OutRunOptions) {
        if (ArgCount < 2) {
            return false;
        }

        OutRunOptions = RunOptions{};
        OutRunOptions.InputFileName = std::string{ ArgValues[1] };

        for (int Index{ 2 }; Index < ArgCount; ++Index) {
            const std::string Argument{ ArgValues[Index] };
            const std::string Prefix{ "--flip-uv=" };
            if (Argument.rfind(Prefix, 0) == 0) {
                bool IsUvFlipEnabled{ OutRunOptions.IsUvFlipEnabled };
                const std::string Value{ Argument.substr(Prefix.size()) };
                if (!TryParseUvFlipValue(Value, IsUvFlipEnabled)) {
                    return false;
                }
                OutRunOptions.IsUvFlipEnabled = IsUvFlipEnabled;
                continue;
            }

            return false;
        }

        return true;
    }

    std::filesystem::path GetProjectDirectoryPath() {
        return std::filesystem::current_path();
    }

    std::filesystem::path GetAssetFilePath(const std::filesystem::path& ProjectDirectoryPath, const std::string& InputFileName) {
        return ProjectDirectoryPath / "Asset" / InputFileName;
    }

    std::filesystem::path GetBinDirectoryPath(const std::filesystem::path& ProjectDirectoryPath) {
        return ProjectDirectoryPath / "Bin";
    }

    std::filesystem::path GetImagesDirectoryPath(const std::filesystem::path& BinDirectoryPath) {
        return BinDirectoryPath / "Images";
    }

    std::filesystem::path GetBinaryOutputPath(const std::filesystem::path& BinDirectoryPath, const std::filesystem::path& AssetFilePath) {
        return BinDirectoryPath / (AssetFilePath.stem().string() + ".bin");
    }

    std::filesystem::path GetMaterialOutputPath(const std::filesystem::path& BinDirectoryPath, const std::filesystem::path& AssetFilePath) {
        return BinDirectoryPath / (AssetFilePath.stem().string() + "_materials.json");
    }

    std::size_t CountTotalVertices(const asset::ModelResult& ModelResultData) {
        std::size_t TotalVertices{ 0 };

        ModelResultData.ForEachDfs([&TotalVertices](asset::ModelNode& Node) {
            TotalVertices += Node.Vertices().VertexCount();
        });

        return TotalVertices;
    }

    std::size_t CountTotalIndices(const asset::ModelResult& ModelResultData) {
        std::size_t TotalIndices{ 0 };

        ModelResultData.ForEachDfs([&TotalIndices](asset::ModelNode& Node) {
            TotalIndices += Node.Indices().size();
        });

        return TotalIndices;
    }

    std::size_t CountTotalSkinClusterLinks(const asset::ModelResult& ModelResultData) {
        const asset::SkeletonData& SkeletonDataValue{ ModelResultData.GetSkeletonData() };
        std::size_t TotalClusterLinks{ 0 };
        for (const asset::SkeletonSkin& SkinData : SkeletonDataValue.Skins) {
            TotalClusterLinks += SkinData.ClusterIndices.size();
        }
        return TotalClusterLinks;
    }

    int Run(const RunOptions& Options) {
        const std::filesystem::path ProjectDirectoryPath{ GetProjectDirectoryPath() };
        const std::filesystem::path AssetFilePath{ GetAssetFilePath(ProjectDirectoryPath, Options.InputFileName) };
        const std::filesystem::path BinDirectoryPath{ GetBinDirectoryPath(ProjectDirectoryPath) };
        const std::filesystem::path BinaryOutputPath{ GetBinaryOutputPath(BinDirectoryPath, AssetFilePath) };
        const std::filesystem::path MaterialOutputPath{ GetMaterialOutputPath(BinDirectoryPath, AssetFilePath) };
        const std::filesystem::path ImagesDirectoryPath{ GetImagesDirectoryPath(BinDirectoryPath) };

        if (!std::filesystem::exists(AssetFilePath)) {
            StdOutput::PrintErrorLine("[AssetZIP] Input FBX file was not found: {}", AssetFilePath.string());
            return 1;
        }

        std::error_code ErrorCode{};
        std::filesystem::create_directories(BinDirectoryPath, ErrorCode);
        if (ErrorCode) {
            StdOutput::PrintErrorLine("[AssetZIP] Failed to create Bin directory: {}", BinDirectoryPath.string());
            return 1;
        }

        asset::FbxAssetImporter FbxAssetImporterData{ asset::GraphicsAPI::DirectX };
        asset::ModelResult ModelData{};
        std::vector<asset::MaterialGroup> MaterialGroups{};
        FbxAssetImporterData.LoadFromFile(AssetFilePath.string(), ModelData, MaterialGroups, Options.IsUvFlipEnabled);

        asset::AssetBinaryWriter AssetBinaryWriterData{};
        const bool IsBinaryWriteSuccess{ AssetBinaryWriterData.WriteToFile(BinaryOutputPath.string(), ModelData) };
        if (!IsBinaryWriteSuccess) {
            StdOutput::PrintErrorLine("[AssetZIP] Failed to create binary file: {}", BinaryOutputPath.string());
            return 1;
        }

        asset::MaterialGroupJsonSerializer MaterialGroupJsonSerializerData{};
        const bool IsMaterialWriteSuccess{ MaterialGroupJsonSerializerData.WriteToFile(MaterialOutputPath.string(), MaterialGroups) };
        if (!IsMaterialWriteSuccess) {
            StdOutput::PrintErrorLine("[AssetZIP] Failed to create material JSON file: {}", MaterialOutputPath.string());
            return 1;
        }

        const asset::ModelResult& ModelResultData{ ModelData };
        const std::size_t TotalVertices{ CountTotalVertices(ModelResultData) };
        const std::size_t TotalIndices{ CountTotalIndices(ModelResultData) };
        const std::size_t MaterialGroupCount{ MaterialGroups.size() };
        const asset::SkeletonData& SkeletonDataValue{ ModelResultData.GetSkeletonData() };
        const std::size_t BoneCount{ SkeletonDataValue.Bones.size() };
        const std::size_t ClusterCount{ SkeletonDataValue.Clusters.size() };
        const std::size_t SkinCount{ SkeletonDataValue.Skins.size() };
        const std::size_t TotalClusterLinks{ CountTotalSkinClusterLinks(ModelResultData) };

        StdOutput::PrintLine("[AssetZIP] Input file: {}", AssetFilePath.string());
        StdOutput::PrintLine("[AssetZIP] Output binary: {}", BinaryOutputPath.string());
        StdOutput::PrintLine("[AssetZIP] Output material JSON: {}", MaterialOutputPath.string());

        if (std::filesystem::exists(ImagesDirectoryPath)) {
            StdOutput::PrintLine("[AssetZIP] Embedded image output directory: {}", ImagesDirectoryPath.string());
        }

        StdOutput::PrintLine("[AssetZIP] Node count: {}", ModelResultData.NodeCount());
        StdOutput::PrintLine("[AssetZIP] Total vertices: {}", TotalVertices);
        StdOutput::PrintLine("[AssetZIP] Total indices: {}", TotalIndices);
        StdOutput::PrintLine("[AssetZIP] Skeleton bone count: {}", BoneCount);
        StdOutput::PrintLine("[AssetZIP] Skeleton cluster count: {}", ClusterCount);
        StdOutput::PrintLine("[AssetZIP] Skeleton skin count: {}", SkinCount);
        StdOutput::PrintLine("[AssetZIP] Skeleton skin-cluster link count: {}", TotalClusterLinks);
        StdOutput::PrintLine("[AssetZIP] Material group count: {}", MaterialGroupCount);
        StdOutput::PrintLine("[AssetZIP] UV flip enabled: {}", Options.IsUvFlipEnabled ? "true" : "false");

        return 0;
    }
}

int main(int ArgCount, char* ArgValues[]) {
    RunOptions Options{};
    if (!TryParseArguments(ArgCount, ArgValues, Options)) {
        StdOutput::PrintErrorLine("[AssetZIP] Usage: AssetZIP <FBXFileName> [--flip-uv=true|false]");
        return 1;
    }

    try {
        return Run(Options);
    }
    catch (const std::exception& ExceptionData) {
        StdOutput::PrintErrorLine("[AssetZIP] {}", ExceptionData.what());
        return 1;
    }
}
