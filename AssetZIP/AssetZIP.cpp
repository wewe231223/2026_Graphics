#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "Asset/AssetBinaryWriter.h"
#include "Asset/AssimpAssetImporter.h"
#include "Asset/MaterialGroupJsonSerializer.h"
#include "Asset/ModelResult.h"
#include "Utility/StdOutput.h"
#include "Utility/StringUtils.h"

#pragma comment(lib, "Asset.lib")

namespace {
    std::string ToLowercase(std::string Value) {
        std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char Character) {
            return static_cast<char>(std::tolower(Character));
            });
        return Value;
    }

    struct Options final {
    public:
        std::string InputFileName{};
        bool IsUvFlipEnabled{ false };
    };

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

        StdOutput::PrintLine("[AssetZIP] Input file: {}", AssetFilePath.string());
        StdOutput::PrintLine("[AssetZIP] Output binary: {}", BinaryOutputPath.string());
        StdOutput::PrintLine("[AssetZIP] Output material JSON: {}", MaterialOutputPath.string());
        StdOutput::PrintLine("[AssetZIP] Node count: {}", ModelData.NodeCount());
        StdOutput::PrintLine("[AssetZIP] Material group count: {}", MaterialGroups.size());
        StdOutput::PrintLine("[AssetZIP] UV flip enabled: {}", ParsedOptions.IsUvFlipEnabled ? "true" : "false");
        return 0;
    }
    catch (const std::exception& ExceptionData) {
        StdOutput::PrintErrorLine("[AssetZIP] {}", ExceptionData.what());
        return 1;
    }
}
