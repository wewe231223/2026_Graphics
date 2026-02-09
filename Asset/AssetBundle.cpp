#include "AssetBundle.h"

using namespace asset;

AssetBundle::AssetBundle() = default;

ModelResult& AssetBundle::GetModelResult() {
    return mModelResult;
}

const ModelResult& AssetBundle::GetModelResult() const {
    return mModelResult;
}

std::vector<Material>& AssetBundle::GetMaterials() {
    return mMaterials;
}

const std::vector<Material>& AssetBundle::GetMaterials() const {
    return mMaterials;
}

void AssetBundle::Clear() {
    mModelResult = ModelResult{};
    mMaterials.clear();
}
