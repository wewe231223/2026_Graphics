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
    std::filesystem::path GetProjectDirectoryPath() {
        return std::filesystem::current_path();
    }

    std::filesystem::path GetAssetFilePath(const std::filesystem::path& ProjectDirectoryPath, const std::string& InputFileName) {
        return ProjectDirectoryPath / "Asset" / InputFileName;
    }

    std::filesystem::path GetBinDirectoryPath(const std::filesystem::path& ProjectDirectoryPath) {
        return ProjectDirectoryPath / "Bin";
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

    int Run(const std::string& InputFileName) {
        const std::filesystem::path ProjectDirectoryPath{ GetProjectDirectoryPath() };
        const std::filesystem::path AssetFilePath{ GetAssetFilePath(ProjectDirectoryPath, InputFileName) };
        const std::filesystem::path BinDirectoryPath{ GetBinDirectoryPath(ProjectDirectoryPath) };
        const std::filesystem::path BinaryOutputPath{ GetBinaryOutputPath(BinDirectoryPath, AssetFilePath) };
        const std::filesystem::path MaterialOutputPath{ GetMaterialOutputPath(BinDirectoryPath, AssetFilePath) };

        if (!std::filesystem::exists(AssetFilePath)) {
            StdOutput::PrintErrorLine("입력 FBX 파일을 찾을 수 없습니다 : {}", AssetFilePath.string());
            return 1;
        }

        std::error_code ErrorCode{};
        std::filesystem::create_directories(BinDirectoryPath, ErrorCode);
        if (ErrorCode) {
            StdOutput::PrintErrorLine("Bin 폴더 생성에 실패하였습니다 : {}", BinDirectoryPath.string());
            return 1;
        }

        asset::FbxAssetImporter FbxAssetImporterData{ asset::GraphicsAPI::DirectX };
        asset::ModelResult ModelData{};
        std::vector<asset::MaterialGroup> MaterialGroups{};
        FbxAssetImporterData.LoadFromFile(AssetFilePath.string(), ModelData, MaterialGroups);

        asset::AssetBinaryWriter AssetBinaryWriterData{};
        const bool IsBinaryWriteSuccess{ AssetBinaryWriterData.WriteToFile(BinaryOutputPath.string(), ModelData) };
        if (!IsBinaryWriteSuccess) {
            StdOutput::PrintErrorLine("바이너리 파일 생성에 실패하였습니다 : {}", BinaryOutputPath.string());
            return 1;
        }

        asset::MaterialGroupJsonSerializer MaterialGroupJsonSerializerData{};
        const bool IsMaterialWriteSuccess{ MaterialGroupJsonSerializerData.WriteToFile(MaterialOutputPath.string(), MaterialGroups) };
        if (!IsMaterialWriteSuccess) {
            StdOutput::PrintErrorLine("재질 JSON 파일 생성에 실패하였습니다 : {}", MaterialOutputPath.string());
            return 1;
        }

        const asset::ModelResult& ModelResultData{ ModelData };
        const std::size_t TotalVertices{ CountTotalVertices(ModelResultData) };
        const std::size_t TotalIndices{ CountTotalIndices(ModelResultData) };
        const std::size_t MaterialGroupCount{ MaterialGroups.size() };

        StdOutput::PrintLine("입력 파일: {}", AssetFilePath.string());
        StdOutput::PrintLine("출력 바이너리: {}", BinaryOutputPath.string());
        StdOutput::PrintLine("출력 재질 JSON: {}", MaterialOutputPath.string());
        StdOutput::PrintLine("노드 수: {}", ModelResultData.NodeCount());
        StdOutput::PrintLine("총 정점 수: {}", TotalVertices);
        StdOutput::PrintLine("총 인덱스 수: {}", TotalIndices);
        StdOutput::PrintLine("재질 그룹 개수: {}", MaterialGroupCount);

        return 0;
    }
}

int main(int ArgCount, char* ArgValues[]) {
    if (ArgCount < 2) {
        StdOutput::PrintErrorLine("사용법: AssetZIP <FBX파일명");
        return 1;
    }

    const std::string InputFileName{ ArgValues[1] };
    return Run(InputFileName);
}
