#pragma once

#include <string_view>

#include "AnimationClipResult.h"
#include "Common.h"

#ifdef _DEBUG
#pragma comment(lib, "assimp-vc143-mtd.lib")
#else
#pragma comment(lib, "assimp-vc143-mt.lib")
#endif

namespace asset {
    class AssimpAnimationClipImporter final {
    public:
        explicit AssimpAnimationClipImporter(GraphicsAPI Api);
        ~AssimpAnimationClipImporter() = default;
        AssimpAnimationClipImporter(const AssimpAnimationClipImporter& Other) = delete;
        AssimpAnimationClipImporter& operator=(const AssimpAnimationClipImporter& Other) = delete;
        AssimpAnimationClipImporter(AssimpAnimationClipImporter&& Other) noexcept = default;
        AssimpAnimationClipImporter& operator=(AssimpAnimationClipImporter&& Other) noexcept = default;

    public:
        void LoadFromFile(std::string_view FilePath, AnimationClipResult& OutClipData);

    private:
        GraphicsAPI mApi{ GraphicsAPI::DirectX };
    };
}
