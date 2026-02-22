#include "AssetBundle.h"

using namespace asset;

AssetBundle::AssetBundle() = default;

ModelResult& AssetBundle::GetModelResult() {
    return mModelResult;
}

const ModelResult& AssetBundle::GetModelResult() const {
    return mModelResult;
}

std::vector<MaterialGroup>& AssetBundle::GetMaterialGroups() {
    return mMaterialGroups;
}

const std::vector<MaterialGroup>& AssetBundle::GetMaterialGroups() const {
    return mMaterialGroups;
}

void AssetBundle::Clear() {
    mModelResult = ModelResult{};
    mMaterialGroups.clear();
}
