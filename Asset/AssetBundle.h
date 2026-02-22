#pragma once

#include <vector>

#include "Common.h"
#include "ModelResult.h"

namespace asset {
    class AssetBundle final {
    public:
        AssetBundle();
        ~AssetBundle() = default;

        AssetBundle(const AssetBundle& Other) = delete;
        AssetBundle& operator=(const AssetBundle& Other) = delete;
        AssetBundle(AssetBundle&& Other) noexcept = default;
        AssetBundle& operator=(AssetBundle&& Other) noexcept = default;

    public:
        ModelResult& GetModelResult();
        const ModelResult& GetModelResult() const;

        std::vector<MaterialGroup>& GetMaterialGroups();
        const std::vector<MaterialGroup>& GetMaterialGroups() const;

        void Clear();

    private:
        ModelResult mModelResult{};
        std::vector<MaterialGroup> mMaterialGroups{};
    };
}
