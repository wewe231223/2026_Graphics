#pragma once

#include <string_view>
#include <vector>

#include "Common.h"
#include "ModelResult.h"

namespace asset {
    class GltfAssetImporter final {
    public:
        explicit GltfAssetImporter(GraphicsAPI Api);
        ~GltfAssetImporter() = default;

        GltfAssetImporter(const GltfAssetImporter& Other) = delete;
        GltfAssetImporter& operator=(const GltfAssetImporter& Other) = delete;
        GltfAssetImporter(GltfAssetImporter&& Other) noexcept = default;
        GltfAssetImporter& operator=(GltfAssetImporter&& Other) noexcept = default;

    public:
        void LoadFromFile(std::string_view FilePath, ModelResult& OutModelData, std::vector<MaterialGroup>& OutMaterialGroups, bool IsUvFlipEnabled);

    private:
        GraphicsAPI mApi{ GraphicsAPI::DirectX };
    };
}
