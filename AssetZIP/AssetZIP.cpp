#include <exception>
#include <cstdint>
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


    void PrintBoneData(const asset::SkeletonData& SkeletonDataValue) {
        if (SkeletonDataValue.Bones.empty()) {
            return;
        }

        StdOutput::PrintLine("[AssetZIP] ----- Skeleton Bone Data Begin -----");

        for (std::size_t BoneArrayIndex{ 0 }; BoneArrayIndex < SkeletonDataValue.Bones.size(); ++BoneArrayIndex) {
            const asset::SkeletonBone& BoneData{ SkeletonDataValue.Bones[BoneArrayIndex] };
            StdOutput::PrintLine("[AssetZIP] Bone[{}].Name: {}", BoneArrayIndex, BoneData.Name);
            StdOutput::PrintLine("[AssetZIP] Bone[{}].NodeName: {}", BoneArrayIndex, BoneData.NodeName);
            StdOutput::PrintLine("[AssetZIP] Bone[{}].NodeTypedId: {}", BoneArrayIndex, BoneData.NodeTypedId);
            StdOutput::PrintLine("[AssetZIP] Bone[{}].BoneTypedId: {}", BoneArrayIndex, BoneData.BoneTypedId);
        }

        StdOutput::PrintLine("[AssetZIP] ----- Skeleton Bone Data End -----");
    }

    void PrintClusterData(const asset::SkeletonData& SkeletonDataValue) {
        if (SkeletonDataValue.Clusters.empty()) {
            return;
        }

        StdOutput::PrintLine("[AssetZIP] ----- Skeleton Cluster Data Begin -----");

        for (std::size_t ClusterArrayIndex{ 0 }; ClusterArrayIndex < SkeletonDataValue.Clusters.size(); ++ClusterArrayIndex) {
            const asset::SkeletonCluster& ClusterData{ SkeletonDataValue.Clusters[ClusterArrayIndex] };
            StdOutput::PrintLine("[AssetZIP] Cluster[{}].Name: {}", ClusterArrayIndex, ClusterData.Name);
            StdOutput::PrintLine("[AssetZIP] Cluster[{}].ClusterTypedId: {}", ClusterArrayIndex, ClusterData.ClusterTypedId);
            StdOutput::PrintLine("[AssetZIP] Cluster[{}].SkinDeformerTypedId: {}", ClusterArrayIndex, ClusterData.SkinDeformerTypedId);
            StdOutput::PrintLine("[AssetZIP] Cluster[{}].BoneIndex: {}", ClusterArrayIndex, ClusterData.BoneIndex);
        }

        StdOutput::PrintLine("[AssetZIP] ----- Skeleton Cluster Data End -----");
    }

    void PrintUnreferencedBones(const asset::SkeletonData& SkeletonDataValue) {
        if (SkeletonDataValue.Bones.empty()) {
            return;
        }

        std::vector<std::uint8_t> IsBoneReferenced{};
        IsBoneReferenced.resize(SkeletonDataValue.Bones.size(), static_cast<std::uint8_t>(0));

        for (const asset::SkeletonCluster& ClusterData : SkeletonDataValue.Clusters) {
            if (static_cast<std::size_t>(ClusterData.BoneIndex) < IsBoneReferenced.size()) {
                IsBoneReferenced[ClusterData.BoneIndex] = static_cast<std::uint8_t>(1);
            }
        }

        std::size_t UnreferencedBoneCount{ 0 };
        for (std::size_t BoneIndex{ 0 }; BoneIndex < IsBoneReferenced.size(); ++BoneIndex) {
            if (IsBoneReferenced[BoneIndex] == static_cast<std::uint8_t>(0)) {
                ++UnreferencedBoneCount;
            }
        }

        StdOutput::PrintLine("[AssetZIP] Skeleton unreferenced bone count: {}", UnreferencedBoneCount);
        if (UnreferencedBoneCount == 0) {
            return;
        }

        for (std::size_t BoneIndex{ 0 }; BoneIndex < SkeletonDataValue.Bones.size(); ++BoneIndex) {
            if (IsBoneReferenced[BoneIndex] != static_cast<std::uint8_t>(0)) {
                continue;
            }

            const asset::SkeletonBone& BoneData{ SkeletonDataValue.Bones[BoneIndex] };
            StdOutput::PrintLine("[AssetZIP] UnreferencedBone[{}].Name: {}", BoneIndex, BoneData.Name);
            StdOutput::PrintLine("[AssetZIP] UnreferencedBone[{}].NodeName: {}", BoneIndex, BoneData.NodeName);
            StdOutput::PrintLine("[AssetZIP] UnreferencedBone[{}].NodeTypedId: {}", BoneIndex, BoneData.NodeTypedId);
            StdOutput::PrintLine("[AssetZIP] UnreferencedBone[{}].BoneTypedId: {}", BoneIndex, BoneData.BoneTypedId);
        }
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
        StdOutput::PrintLine("[AssetZIP] Skeleton bone global cluster index count: {}", ClusterCount);
        StdOutput::PrintLine("[AssetZIP] Skeleton skin metadata recorded in binary: false");
        PrintUnreferencedBones(SkeletonDataValue);
        PrintBoneData(SkeletonDataValue);
        PrintClusterData(SkeletonDataValue);
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
