#include "AssetRegistry.h"
#include "Asset/AssetBinaryReader.h"
#include "Asset/MaterialGroupJsonSerializer.h"

namespace Game {
    AssetRegistry::AssetRegistry()
        : mDevice{ nullptr },
        mCopyQueue{ nullptr },
        mAllocator{ nullptr },
        mModelCache{},
        mLoadedMaterialJsonPaths{},
        mMaterials{},
        mMaterialNameLookup{},
        mMaterialGroups{},
        mMaterialGroupNameLookup{},
        mMaterialGroupSourcePaths{},
        mPipelines{},
        mPipelineLookup{} {
    }

    AssetRegistry::~AssetRegistry() {
    }

    AssetRegistry::AssetRegistry(AssetRegistry&& Other) noexcept
        : mDevice{ Other.mDevice },
        mCopyQueue{ Other.mCopyQueue },
        mAllocator{ Other.mAllocator },
        mModelCache{ std::move(Other.mModelCache) },
        mLoadedMaterialJsonPaths{ std::move(Other.mLoadedMaterialJsonPaths) },
        mMaterials{ std::move(Other.mMaterials) },
        mMaterialNameLookup{ std::move(Other.mMaterialNameLookup) },
        mMaterialGroups{ std::move(Other.mMaterialGroups) },
        mMaterialGroupNameLookup{ std::move(Other.mMaterialGroupNameLookup) },
        mMaterialGroupSourcePaths{ std::move(Other.mMaterialGroupSourcePaths) },
        mPipelines{ std::move(Other.mPipelines) },
        mPipelineLookup{ std::move(Other.mPipelineLookup) } {
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
        mLoadedMaterialJsonPaths = std::move(Other.mLoadedMaterialJsonPaths);
        mMaterials = std::move(Other.mMaterials);
        mMaterialNameLookup = std::move(Other.mMaterialNameLookup);
        mMaterialGroups = std::move(Other.mMaterialGroups);
        mMaterialGroupNameLookup = std::move(Other.mMaterialGroupNameLookup);
        mMaterialGroupSourcePaths = std::move(Other.mMaterialGroupSourcePaths);
        mPipelines = std::move(Other.mPipelines);
        mPipelineLookup = std::move(Other.mPipelineLookup);

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

    std::shared_ptr<Model> AssetRegistry::GetModel(const std::string& ModelBinaryPath) {
        const auto FoundModel{ mModelCache.find(ModelBinaryPath) };
        if (FoundModel != mModelCache.end()) {
            return FoundModel->second;
        }

        asset::ModelResult ModelData{};
        if (ReadModelData(ModelBinaryPath, ModelData) == false) {
            return nullptr;
        }

        std::shared_ptr<Model> NewModel{ std::make_shared<Model>() };
        if (mDevice != nullptr && mCopyQueue != nullptr && mAllocator != nullptr) {
            const bool IsInitialized{ NewModel->InitializeFromModelResult(ModelData, *mAllocator, *mCopyQueue) };
            if (IsInitialized == false) {
                return nullptr;
            }
        }

        mModelCache.insert_or_assign(ModelBinaryPath, NewModel);
        return NewModel;
    }

    bool AssetRegistry::LoadMaterialGroups(const std::string& MaterialJsonPath) {
        if (mLoadedMaterialJsonPaths.find(MaterialJsonPath) != mLoadedMaterialJsonPaths.end()) {
            return true;
        }

        std::vector<asset::MaterialGroup> MaterialGroups{};
        if (ReadMaterialGroups(MaterialJsonPath, MaterialGroups) == false) {
            return false;
        }

        for (const asset::MaterialGroup& MaterialGroupData : MaterialGroups) {
            AddMaterialGroupWithSource(MaterialGroupData, MaterialJsonPath);
        }

        mLoadedMaterialJsonPaths.insert(MaterialJsonPath);
        return true;
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

    std::uint32_t AssetRegistry::AddMaterialGroup(const asset::MaterialGroup& MaterialGroupData) {
        return AddMaterialGroupWithSource(MaterialGroupData, std::string{});
    }

    std::uint32_t AssetRegistry::AddMaterialGroupWithSource(const asset::MaterialGroup& MaterialGroupData, const std::string& SourcePath) {
        std::string MaterialGroupName{ MaterialGroupData.Name };
        if (MaterialGroupName.empty()) {
            MaterialGroupName = std::string{ "MaterialGroup_" } + std::to_string(mMaterialGroups.size());
        }

        const auto FoundMaterialGroup{ mMaterialGroupNameLookup.find(MaterialGroupName) };
        if (FoundMaterialGroup != mMaterialGroupNameLookup.end()) {
            const std::uint32_t ExistingIndex{ FoundMaterialGroup->second };
            if (SourcePath.empty() == false && ExistingIndex < mMaterialGroupSourcePaths.size() && mMaterialGroupSourcePaths[ExistingIndex].empty()) {
                mMaterialGroupSourcePaths[ExistingIndex] = SourcePath;
            }

            return ExistingIndex;
        }

        RegisteredMaterialGroup NewMaterialGroup{};
        NewMaterialGroup.Name = MaterialGroupName;
        NewMaterialGroup.Items.reserve(MaterialGroupData.Items.size());
        for (const asset::MaterialGroupItem& MaterialGroupItemData : MaterialGroupData.Items) {
            RegisteredMaterialGroupItem NewMaterialGroupItem{};
            NewMaterialGroupItem.MaterialIndex = AddMaterial(MaterialGroupItemData.MaterialData);
            NewMaterialGroupItem.Pipeline = ResolvePipelineByName(MaterialGroupItemData.PipelineName);
            NewMaterialGroup.Items.push_back(NewMaterialGroupItem);
        }

        const std::uint32_t NewIndex{ static_cast<std::uint32_t>(mMaterialGroups.size()) };
        mMaterialGroups.push_back(std::move(NewMaterialGroup));
        mMaterialGroupSourcePaths.push_back(SourcePath);
        mMaterialGroupNameLookup.insert_or_assign(mMaterialGroups.back().Name, NewIndex);
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

    const std::vector<RegisteredMaterialGroup>& AssetRegistry::GetMaterialGroups() const {
        return mMaterialGroups;
    }



    std::uint32_t AssetRegistry::FindMaterialGroupIndexBySourcePath(const std::string& MaterialSourcePath) const {
        if (MaterialSourcePath.empty()) {
            return static_cast<std::uint32_t>(-1);
        }

        for (std::uint32_t MaterialGroupIndex{ 0 }; MaterialGroupIndex < mMaterialGroupSourcePaths.size(); ++MaterialGroupIndex) {
            if (mMaterialGroupSourcePaths[MaterialGroupIndex] == MaterialSourcePath) {
                return MaterialGroupIndex;
            }
        }

        return static_cast<std::uint32_t>(-1);
    }

    Interface::IPipeline* AssetRegistry::ResolvePipelineByName(const std::string& PipelineName) {
        if (PipelineName.empty()) {
            return nullptr;
        }

        const auto FoundPipeline{ mPipelineLookup.find(PipelineName) };
        if (FoundPipeline != mPipelineLookup.end()) {
            return FoundPipeline->second;
        }

        std::unique_ptr<Base::Pipeline> NewPipeline{ std::make_unique<Base::Pipeline>() };
        if (NewPipeline->Initialize(PipelineName) == false) {
            mPipelineLookup.insert_or_assign(PipelineName, nullptr);
            return nullptr;
        }

        Interface::IPipeline* PipelinePointer{ NewPipeline.get() };
        mPipelines.push_back(std::move(NewPipeline));
        mPipelineLookup.insert_or_assign(PipelineName, PipelinePointer);
        return PipelinePointer;
    }

    bool AssetRegistry::ReadModelData(const std::string& ModelBinaryPath, asset::ModelResult& OutModelData) const {
        asset::AssetBinaryReader Reader{};
        return Reader.ReadFromFile(ModelBinaryPath, OutModelData);
    }

    bool AssetRegistry::ReadMaterialGroups(const std::string& MaterialJsonPath, std::vector<asset::MaterialGroup>& OutMaterialGroups) const {
        asset::MaterialGroupJsonSerializer Serializer{};
        return Serializer.ReadFromFile(MaterialJsonPath, OutMaterialGroups);
    }
}
