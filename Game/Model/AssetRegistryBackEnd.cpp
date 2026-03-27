#include "AssetRegistryBackEnd.h"

namespace Game {
    AssetRegistryStorage::AssetRegistryStorage()
        : mModelBucket{},
        mAnimationBucket{},
        mAnimationGraphBucket{},
        mMaterialBucket{},
        mMaterialGroupBucket{},
        mTextureTableBucket{},
        mLoadedMaterialJsonPaths{},
        mTextureRecords{},
        mMaterialToTextureTableIndices{},
        mMaterialGroupSourcePaths{},
        mMaterialGroupSourcePathLookup{},
        mPipelines{},
        mPipelineLookup{} {
    }

    AssetRegistryStorage::~AssetRegistryStorage() {
    }

    AssetRegistryStorage::AssetRegistryStorage(AssetRegistryStorage&& Other) noexcept
        : mModelBucket{ std::move(Other.mModelBucket) },
        mAnimationBucket{ std::move(Other.mAnimationBucket) },
        mAnimationGraphBucket{ std::move(Other.mAnimationGraphBucket) },
        mMaterialBucket{ std::move(Other.mMaterialBucket) },
        mMaterialGroupBucket{ std::move(Other.mMaterialGroupBucket) },
        mTextureTableBucket{ std::move(Other.mTextureTableBucket) },
        mLoadedMaterialJsonPaths{ std::move(Other.mLoadedMaterialJsonPaths) },
        mTextureRecords{ std::move(Other.mTextureRecords) },
        mMaterialToTextureTableIndices{ std::move(Other.mMaterialToTextureTableIndices) },
        mMaterialGroupSourcePaths{ std::move(Other.mMaterialGroupSourcePaths) },
        mMaterialGroupSourcePathLookup{ std::move(Other.mMaterialGroupSourcePathLookup) },
        mPipelines{ std::move(Other.mPipelines) },
        mPipelineLookup{ std::move(Other.mPipelineLookup) } {
    }

    AssetRegistryStorage& AssetRegistryStorage::operator=(AssetRegistryStorage&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mModelBucket = std::move(Other.mModelBucket);
        mAnimationBucket = std::move(Other.mAnimationBucket);
        mAnimationGraphBucket = std::move(Other.mAnimationGraphBucket);
        mMaterialBucket = std::move(Other.mMaterialBucket);
        mMaterialGroupBucket = std::move(Other.mMaterialGroupBucket);
        mTextureTableBucket = std::move(Other.mTextureTableBucket);
        mLoadedMaterialJsonPaths = std::move(Other.mLoadedMaterialJsonPaths);
        mTextureRecords = std::move(Other.mTextureRecords);
        mMaterialToTextureTableIndices = std::move(Other.mMaterialToTextureTableIndices);
        mMaterialGroupSourcePaths = std::move(Other.mMaterialGroupSourcePaths);
        mMaterialGroupSourcePathLookup = std::move(Other.mMaterialGroupSourcePathLookup);
        mPipelines = std::move(Other.mPipelines);
        mPipelineLookup = std::move(Other.mPipelineLookup);
        return *this;
    }

    std::unordered_set<std::string>& AssetRegistryStorage::GetLoadedMaterialJsonPaths() {
        return mLoadedMaterialJsonPaths;
    }

    const std::unordered_set<std::string>& AssetRegistryStorage::GetLoadedMaterialJsonPaths() const {
        return mLoadedMaterialJsonPaths;
    }

    std::vector<AssetRegistryTextureRecord>& AssetRegistryStorage::GetTextureRecords() {
        return mTextureRecords;
    }

    const std::vector<AssetRegistryTextureRecord>& AssetRegistryStorage::GetTextureRecords() const {
        return mTextureRecords;
    }

    std::vector<std::vector<std::uint32_t>>& AssetRegistryStorage::GetMaterialToTextureTableIndices() {
        return mMaterialToTextureTableIndices;
    }

    const std::vector<std::vector<std::uint32_t>>& AssetRegistryStorage::GetMaterialToTextureTableIndices() const {
        return mMaterialToTextureTableIndices;
    }

    std::vector<std::string>& AssetRegistryStorage::GetMaterialGroupSourcePaths() {
        return mMaterialGroupSourcePaths;
    }

    const std::vector<std::string>& AssetRegistryStorage::GetMaterialGroupSourcePaths() const {
        return mMaterialGroupSourcePaths;
    }

    std::unordered_map<std::string, std::uint32_t>& AssetRegistryStorage::GetMaterialGroupSourcePathLookup() {
        return mMaterialGroupSourcePathLookup;
    }

    const std::unordered_map<std::string, std::uint32_t>& AssetRegistryStorage::GetMaterialGroupSourcePathLookup() const {
        return mMaterialGroupSourcePathLookup;
    }

    std::vector<std::unique_ptr<Base::Pipeline>>& AssetRegistryStorage::GetPipelines() {
        return mPipelines;
    }

    std::unordered_map<std::string, Interface::IPipeline*>& AssetRegistryStorage::GetPipelineLookup() {
        return mPipelineLookup;
    }

    const std::unordered_map<std::string, Interface::IPipeline*>& AssetRegistryStorage::GetPipelineLookup() const {
        return mPipelineLookup;
    }

    AssetRegistryBackEnd::AssetRegistryBackEnd()
        : mStorage{} {
    }

    AssetRegistryBackEnd::~AssetRegistryBackEnd() {
    }

    AssetRegistryBackEnd::AssetRegistryBackEnd(AssetRegistryBackEnd&& Other) noexcept
        : mStorage{ std::move(Other.mStorage) } {
    }

    AssetRegistryBackEnd& AssetRegistryBackEnd::operator=(AssetRegistryBackEnd&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mStorage = std::move(Other.mStorage);
        return *this;
    }

    AssetRegistryStorage& AssetRegistryBackEnd::GetStorage() {
        return mStorage;
    }

    const AssetRegistryStorage& AssetRegistryBackEnd::GetStorage() const {
        return mStorage;
    }
}
