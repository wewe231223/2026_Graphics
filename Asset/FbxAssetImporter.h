#pragma once

#include <string_view>
#include <vector>

#include "Common.h"
#include "ModelResult.h"

namespace asset {
    class FbxAssetImporter final {
    public:
        explicit FbxAssetImporter(GraphicsAPI Api);
        ~FbxAssetImporter() = default;

        FbxAssetImporter(const FbxAssetImporter& Other) = delete;
        FbxAssetImporter& operator=(const FbxAssetImporter& Other) = delete;
        FbxAssetImporter(FbxAssetImporter&& Other) noexcept = default;
        FbxAssetImporter& operator=(FbxAssetImporter&& Other) noexcept = default;

    public:
        void LoadFromFile(std::string_view FilePath, ModelResult& OutModelData, std::vector<MaterialGroup>& OutMaterialGroups, bool IsUvFlipEnabled);

    private:
        GraphicsAPI mApi{ GraphicsAPI::DirectX };
    };
}
