#include "AssetRegistry.h"

namespace Game {
    AssetRegistry::AssetRegistry()
        : mDevice{ nullptr },
        mCopyQueue{ nullptr },
        mAllocator{ nullptr },
        mModelCache{},
        mMaterials{},
        mMaterialNameLookup{} {
    }

    AssetRegistry::~AssetRegistry() {
    }

    AssetRegistry::AssetRegistry(AssetRegistry&& Other) noexcept
        : mDevice{ Other.mDevice },
        mCopyQueue{ Other.mCopyQueue },
        mAllocator{ Other.mAllocator },
        mModelCache{ std::move(Other.mModelCache) },
        mMaterials{ std::move(Other.mMaterials) },
        mMaterialNameLookup{ std::move(Other.mMaterialNameLookup) } {
        Other.mDevice = nullptr;
        Other.mCopyQueue = nullptr;
        Other.mAllocator = nullptr;
    }

    AssetRegistry& AssetRegistry::operator=(AssetRegistry&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mDevice = Other.mDevice;
        mCopyQueue = Other.mCopyQueue;
        mAllocator = Other.mAllocator;
        mModelCache = std::move(Other.mModelCache);
        mMaterials = std::move(Other.mMaterials);
        mMaterialNameLookup = std::move(Other.mMaterialNameLookup);
        Other.mDevice = nullptr;
        Other.mCopyQueue = nullptr;
        Other.mAllocator = nullptr;
        return *this;
    }

    void AssetRegistry::Initialize(ID3D12Device* Device, Core::DX::CopyQueue* CopyQueue, Core::DX::GraphicsAllocator* Allocator) {
        mDevice = Device;
        mCopyQueue = CopyQueue;
        mAllocator = Allocator;
    }

    std::shared_ptr<Model> AssetRegistry::GetModel(const std::string& ModelKey, const asset::AssetBundle& Bundle) {
        const auto FoundModel{ mModelCache.find(ModelKey) };
        if (FoundModel != mModelCache.end()) {
            return FoundModel->second;
        }

        const std::vector<asset::Material>& SourceMaterials{ Bundle.GetMaterials() };
        std::vector<std::size_t> MaterialIndexRemap{};
        MaterialIndexRemap.reserve(SourceMaterials.size());
        for (const asset::Material& SourceMaterial : SourceMaterials) {
            const std::uint32_t RegisteredIndex{ AddMaterial(SourceMaterial) };
            MaterialIndexRemap.push_back(static_cast<std::size_t>(RegisteredIndex));
        }

        std::shared_ptr<Model> NewModel{ std::make_shared<Model>() };
        if (mDevice != nullptr && mCopyQueue != nullptr && mAllocator != nullptr) {
            NewModel->InitializeFromAssetBundle(Bundle, MaterialIndexRemap, *mAllocator, *mCopyQueue);
        }

        mModelCache.insert_or_assign(ModelKey, NewModel);
        return NewModel;
    }

    std::uint32_t AssetRegistry::AddMaterial(const asset::Material& MaterialData) {
        std::string MaterialName{ MaterialData.Name };
        if (MaterialName.empty()) {
            MaterialName = std::string{ "Material_" } + std::to_string(mMaterials.size());
        }

        const auto FoundMaterial{ mMaterialNameLookup.find(MaterialName) };
        if (FoundMaterial != mMaterialNameLookup.end()) {
            return FoundMaterial->second;
        }

        RegisteredMaterial NewMaterial{};
        NewMaterial.Name = MaterialName;
        NewMaterial.Data = MaterialData;
        NewMaterial.Data.Name = MaterialName;

        const std::uint32_t NewIndex{ static_cast<std::uint32_t>(mMaterials.size()) };
        mMaterials.push_back(std::move(NewMaterial));
        mMaterialNameLookup.insert_or_assign(mMaterials.back().Name, NewIndex);
        return NewIndex;
    }

    std::uint32_t AssetRegistry::FindMaterialIndexByName(const std::string& MaterialName) const {
        const auto FoundMaterial{ mMaterialNameLookup.find(MaterialName) };
        if (FoundMaterial == mMaterialNameLookup.end()) {
            return static_cast<std::uint32_t>(-1);
        }

        return FoundMaterial->second;
    }

    const std::vector<RegisteredMaterial>& AssetRegistry::GetMaterials() const {
        return mMaterials;
    }
}
