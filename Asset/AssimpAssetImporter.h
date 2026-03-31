#pragma once

#include <string_view>
#include <vector>

#include "Common.h"
#include "ModelResult.h"

#ifdef _DEBUG
#pragma comment(lib, "assimp-vc143-mtd.lib")
#else 
#pragma comment(lib, "assimp-vc143-mt.lib")
#endif 

namespace asset {
    class AssimpAssetImporter final {
    public:
        explicit AssimpAssetImporter(GraphicsAPI Api);
        ~AssimpAssetImporter() = default;
        AssimpAssetImporter(const AssimpAssetImporter& Other) = delete;
        AssimpAssetImporter& operator=(const AssimpAssetImporter& Other) = delete;
        AssimpAssetImporter(AssimpAssetImporter&& Other) noexcept = default;
        AssimpAssetImporter& operator=(AssimpAssetImporter&& Other) noexcept = default;

    public:
        void LoadFromFile(std::string_view FilePath, ModelResult& OutModelData, std::vector<MaterialGroup>& OutMaterialGroups, bool IsUvFlipEnabled);

    private:
        GraphicsAPI mApi{ GraphicsAPI::DirectX };
    };
}
