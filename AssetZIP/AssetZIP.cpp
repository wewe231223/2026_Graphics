#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include "Asset/AssetBinaryWriter.h"
#include "Asset/FbxAssetImporter.h"
#include "Asset/MaterialGroupJsonSerializer.h"
#include "Asset/ModelResult.h"

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
            std::cerr << "입력 FBX 파일을 찾을 수 없습니다 : " << AssetFilePath.string() << std::endl;
            return 1;
        }

        std::error_code ErrorCode{};
        std::filesystem::create_directories(BinDirectoryPath, ErrorCode);
        if (ErrorCode) {
            std::cerr << "Bin 폴더 생성에 실패하였습니다 : " << BinDirectoryPath.string() << std::endl;
            return 1;
        }

        asset::FbxAssetImporter FbxAssetImporterData{ asset::GraphicsAPI::DirectX };
        asset::AssetBundle AssetBundleData{ FbxAssetImporterData.LoadFromFile(AssetFilePath.string()) };

        asset::AssetBinaryWriter AssetBinaryWriterData{};
        const bool IsBinaryWriteSuccess{ AssetBinaryWriterData.WriteToFile(BinaryOutputPath.string(), AssetBundleData) };
        if (!IsBinaryWriteSuccess) {
            std::cerr << "바이너리 파일 생성에 실패하였습니다 : " << BinaryOutputPath.string() << std::endl;
            return 1;
        }

        asset::MaterialGroupJsonSerializer MaterialGroupJsonSerializerData{};
        const bool IsMaterialWriteSuccess{ MaterialGroupJsonSerializerData.WriteToFile(MaterialOutputPath.string(), AssetBundleData.GetMaterialGroups()) };
        if (!IsMaterialWriteSuccess) {
            std::cerr << "재질 JSON 파일 생성에 실패하였습니다 : " << MaterialOutputPath.string() << std::endl;
            return 1;
        }

        const asset::ModelResult& ModelResultData{ AssetBundleData.GetModelResult() };
        const std::size_t TotalVertices{ CountTotalVertices(ModelResultData) };
        const std::size_t TotalIndices{ CountTotalIndices(ModelResultData) };
        const std::size_t MaterialGroupCount{ AssetBundleData.GetMaterialGroups().size() };

        std::cout << "입력 파일: " << AssetFilePath.string() << std::endl;
        std::cout << "출력 바이너리: " << BinaryOutputPath.string() << std::endl;
        std::cout << "출력 재질 JSON: " << MaterialOutputPath.string() << std::endl;
        std::cout << "노드 수: " << ModelResultData.NodeCount() << std::endl;
        std::cout << "총 정점 수:  " << TotalVertices << std::endl;
        std::cout << "총 인덱스 수: " << TotalIndices << std::endl;
        std::cout << "재질 그룹 개수:  " << MaterialGroupCount << std::endl;

        return 0;
    }
}

int main(int ArgCount, char* ArgValues[]) {
    if (ArgCount < 2) {
        std::cerr << "사용법: AssetZIP <FBX파일명" << std::endl;
        return 1;
    }

    const std::string InputFileName{ ArgValues[1] };
    return Run(InputFileName);
}
