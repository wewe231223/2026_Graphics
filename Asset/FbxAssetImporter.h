#pragma once

#include <string_view>

#include "AssetBundle.h"
#include "Common.h"

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
        AssetBundle LoadFromFile(std::string_view FilePath);

    private:
        GraphicsAPI mApi{ GraphicsAPI::DirectX };
    };
}
