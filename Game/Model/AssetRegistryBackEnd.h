#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#include "Core/DX/Texture.h"
#include "Asset/ModelResult.h"
#include "Asset/AnimationClipResult.h"
#include "Game/Asset/AnimationGraphAsset.h"
#include "Model.h"
#include "Game/Base/Common.h"
#include "Game/Base/Pipeline.h"
#include "Game/Base/RenderFrameData.h"

namespace Game {
    template<typename T, typename TGpu = void>
    struct ResourceBucket final {
        using AssetType = T;
        using GpuType = TGpu;
        using GpuStorageType = std::conditional_t<std::is_void_v<TGpu>, std::monostate, TGpu>;

        std::vector<AssetType> mAssets{};
        std::vector<GpuStorageType> mGpuData{};
        std::unordered_map<std::string, std::uint32_t> mNameLookup{};
    };

    struct RegisteredMaterial final {
        std::string Name{};
        asset::Material Data{};
        RFD::MaterialGpu PackedData{};
    };

    struct RegisteredMaterialGroupItem final {
        Interface::IPipeline* Pipeline{ nullptr };
        std::uint32_t MaterialIndex{ 0 };
    };

    struct RegisteredMaterialGroup final {
        std::string Name{};
        std::vector<RegisteredMaterialGroupItem> Items{};
    };

    struct AssetRegistryTextureRecord final {
        std::string Key{};
        Core::DX::TexPtr Texture{};
        std::uint32_t TableIndex{ 0 };
        bool KeepResident{ false };
    };

    struct ModelBucketTag final {
        using BucketType = ResourceBucket<std::shared_ptr<Model>>;
    };


    struct AnimationBucketTag final {
        using BucketType = ResourceBucket<std::shared_ptr<asset::Animation>>;
    };


    struct AnimationGraphBucketTag final {
        using BucketType = ResourceBucket<std::shared_ptr<AnimationGraphAsset>>;
    };

    struct MaterialBucketTag final {
        using BucketType = ResourceBucket<RegisteredMaterial, RFD::MaterialGpu>;
    };

    struct MaterialGroupBucketTag final {
        using BucketType = ResourceBucket<RegisteredMaterialGroup>;
    };

    struct TextureTableBucketTag final {
        using BucketType = ResourceBucket<RFD::MaterialTextureTableItemGpu>;
    };

    class AssetRegistryStorage final {
    public:
        AssetRegistryStorage();
        ~AssetRegistryStorage();
        AssetRegistryStorage(const AssetRegistryStorage& Other) = delete;
        AssetRegistryStorage& operator=(const AssetRegistryStorage& Other) = delete;
        AssetRegistryStorage(AssetRegistryStorage&& Other) noexcept;
        AssetRegistryStorage& operator=(AssetRegistryStorage&& Other) noexcept;

    public:
        template<typename TBucketTag>
        typename TBucketTag::BucketType& GetBucket();

        template<typename TBucketTag>
        const typename TBucketTag::BucketType& GetBucket() const;

        std::unordered_set<std::string>& GetLoadedMaterialJsonPaths();
        const std::unordered_set<std::string>& GetLoadedMaterialJsonPaths() const;

        std::vector<AssetRegistryTextureRecord>& GetTextureRecords();
        const std::vector<AssetRegistryTextureRecord>& GetTextureRecords() const;

        std::vector<std::vector<std::uint32_t>>& GetMaterialToTextureTableIndices();
        const std::vector<std::vector<std::uint32_t>>& GetMaterialToTextureTableIndices() const;

        std::vector<std::string>& GetMaterialGroupSourcePaths();
        const std::vector<std::string>& GetMaterialGroupSourcePaths() const;

        std::unordered_map<std::string, std::uint32_t>& GetMaterialGroupSourcePathLookup();
        const std::unordered_map<std::string, std::uint32_t>& GetMaterialGroupSourcePathLookup() const;

        std::vector<std::unique_ptr<Base::Pipeline>>& GetPipelines();
        std::unordered_map<std::string, Interface::IPipeline*>& GetPipelineLookup();
        const std::unordered_map<std::string, Interface::IPipeline*>& GetPipelineLookup() const;

    private:
        ResourceBucket<std::shared_ptr<Model>> mModelBucket{};
        ResourceBucket<std::shared_ptr<asset::Animation>> mAnimationBucket{};
        ResourceBucket<std::shared_ptr<AnimationGraphAsset>> mAnimationGraphBucket{};
        ResourceBucket<RegisteredMaterial, RFD::MaterialGpu> mMaterialBucket{};
        ResourceBucket<RegisteredMaterialGroup> mMaterialGroupBucket{};
        ResourceBucket<RFD::MaterialTextureTableItemGpu> mTextureTableBucket{};

        std::unordered_set<std::string> mLoadedMaterialJsonPaths{};
        std::vector<AssetRegistryTextureRecord> mTextureRecords{};
        std::vector<std::vector<std::uint32_t>> mMaterialToTextureTableIndices{};

        std::vector<std::string> mMaterialGroupSourcePaths{};
        std::unordered_map<std::string, std::uint32_t> mMaterialGroupSourcePathLookup{};

        std::vector<std::unique_ptr<Base::Pipeline>> mPipelines{};
        std::unordered_map<std::string, Interface::IPipeline*> mPipelineLookup{};
    };

    class IAssetRegistryBackEnd {
    public:
        IAssetRegistryBackEnd() = default;
        virtual ~IAssetRegistryBackEnd() = default;
        IAssetRegistryBackEnd(const IAssetRegistryBackEnd& Other) = delete;
        IAssetRegistryBackEnd& operator=(const IAssetRegistryBackEnd& Other) = delete;
        IAssetRegistryBackEnd(IAssetRegistryBackEnd&& Other) = delete;
        IAssetRegistryBackEnd& operator=(IAssetRegistryBackEnd&& Other) = delete;

    public:
        virtual AssetRegistryStorage& GetStorage() = 0;
        virtual const AssetRegistryStorage& GetStorage() const = 0;
    };

    class AssetRegistryBackEnd final : public IAssetRegistryBackEnd {
    public:
        AssetRegistryBackEnd();
        ~AssetRegistryBackEnd() override;
        AssetRegistryBackEnd(const AssetRegistryBackEnd& Other) = delete;
        AssetRegistryBackEnd& operator=(const AssetRegistryBackEnd& Other) = delete;
        AssetRegistryBackEnd(AssetRegistryBackEnd&& Other) noexcept;
        AssetRegistryBackEnd& operator=(AssetRegistryBackEnd&& Other) noexcept;

    public:
        AssetRegistryStorage& GetStorage() override;
        const AssetRegistryStorage& GetStorage() const override;

    private:
        AssetRegistryStorage mStorage{};
    };

    template<>
    inline ModelBucketTag::BucketType& AssetRegistryStorage::GetBucket<ModelBucketTag>() {
        return mModelBucket;
    }

    template<>
    inline const ModelBucketTag::BucketType& AssetRegistryStorage::GetBucket<ModelBucketTag>() const {
        return mModelBucket;
    }

    template<>
    inline AnimationBucketTag::BucketType& AssetRegistryStorage::GetBucket<AnimationBucketTag>() {
        return mAnimationBucket;
    }

    template<>
    inline const AnimationBucketTag::BucketType& AssetRegistryStorage::GetBucket<AnimationBucketTag>() const {
        return mAnimationBucket;
    }


    template<>
    inline AnimationGraphBucketTag::BucketType& AssetRegistryStorage::GetBucket<AnimationGraphBucketTag>() {
        return mAnimationGraphBucket;
    }

    template<>
    inline const AnimationGraphBucketTag::BucketType& AssetRegistryStorage::GetBucket<AnimationGraphBucketTag>() const {
        return mAnimationGraphBucket;
    }

    template<>
    inline MaterialBucketTag::BucketType& AssetRegistryStorage::GetBucket<MaterialBucketTag>() {
        return mMaterialBucket;
    }

    template<>
    inline const MaterialBucketTag::BucketType& AssetRegistryStorage::GetBucket<MaterialBucketTag>() const {
        return mMaterialBucket;
    }

    template<>
    inline MaterialGroupBucketTag::BucketType& AssetRegistryStorage::GetBucket<MaterialGroupBucketTag>() {
        return mMaterialGroupBucket;
    }

    template<>
    inline const MaterialGroupBucketTag::BucketType& AssetRegistryStorage::GetBucket<MaterialGroupBucketTag>() const {
        return mMaterialGroupBucket;
    }

    template<>
    inline TextureTableBucketTag::BucketType& AssetRegistryStorage::GetBucket<TextureTableBucketTag>() {
        return mTextureTableBucket;
    }

    template<>
    inline const TextureTableBucketTag::BucketType& AssetRegistryStorage::GetBucket<TextureTableBucketTag>() const {
        return mTextureTableBucket;
    }
}
