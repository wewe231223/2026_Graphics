#include "AssetRegistry.h"
#include <array>
#include <filesystem>
#include <limits>
#include <unordered_set>
#include "Asset/AssetBinaryReader.h"
#include "Asset/MaterialGroupJsonSerializer.h"

#ifdef max 
#undef max
#endif 

namespace {
    constexpr std::uint32_t MaterialFieldCount{ 40 };

    bool IsSupportedMaterialType(asset::MaterialType MaterialTypeValue) {
        const std::uint32_t TypeValue{ static_cast<std::uint32_t>(MaterialTypeValue) };
        return TypeValue < MaterialFieldCount;
    }
}

namespace Game {
    AssetRegistry::AssetRegistry()
        : mDevice{ nullptr },
        mCopyQueue{ nullptr },
        mAllocator{ nullptr },
        mSrvHeap{ nullptr },
        mModelCache{},
        mLoadedMaterialJsonPaths{},
        mMaterials{},
        mPackedMaterials{},
        mMaterialNameLookup{},
        mMaterialTextureTable{},
        mTextureTableLookup{},
        mTextureRecords{},
        mMaterialToTextureTableIndices{},
        mTextureResidencyDecider{},
        mMaterialGroups{},
        mMaterialGroupNameLookup{},
        mMaterialGroupSourcePaths{},
        mMaterialGroupSourcePathLookup{},
        mPipelines{},
        mPipelineLookup{} {
    }

    AssetRegistry::~AssetRegistry() {
    }

    AssetRegistry::AssetRegistry(AssetRegistry&& Other) noexcept
        : mDevice{ Other.mDevice },
        mCopyQueue{ Other.mCopyQueue },
        mAllocator{ Other.mAllocator },
        mSrvHeap{ Other.mSrvHeap },
        mModelCache{ std::move(Other.mModelCache) },
        mLoadedMaterialJsonPaths{ std::move(Other.mLoadedMaterialJsonPaths) },
        mMaterials{ std::move(Other.mMaterials) },
        mPackedMaterials{ std::move(Other.mPackedMaterials) },
        mMaterialNameLookup{ std::move(Other.mMaterialNameLookup) },
        mMaterialTextureTable{ std::move(Other.mMaterialTextureTable) },
        mTextureTableLookup{ std::move(Other.mTextureTableLookup) },
        mTextureRecords{ std::move(Other.mTextureRecords) },
        mMaterialToTextureTableIndices{ std::move(Other.mMaterialToTextureTableIndices) },
        mTextureResidencyDecider{ std::move(Other.mTextureResidencyDecider) },
        mMaterialGroups{ std::move(Other.mMaterialGroups) },
        mMaterialGroupNameLookup{ std::move(Other.mMaterialGroupNameLookup) },
        mMaterialGroupSourcePaths{ std::move(Other.mMaterialGroupSourcePaths) },
        mMaterialGroupSourcePathLookup{ std::move(Other.mMaterialGroupSourcePathLookup) },
        mPipelines{ std::move(Other.mPipelines) },
        mPipelineLookup{ std::move(Other.mPipelineLookup) } {
        Other.mDevice = nullptr;
        Other.mCopyQueue = nullptr;
        Other.mAllocator = nullptr;
        Other.mSrvHeap = nullptr;
    }

    AssetRegistry& AssetRegistry::operator=(AssetRegistry&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mDevice = Other.mDevice;
        mCopyQueue = Other.mCopyQueue;
        mAllocator = Other.mAllocator;
        mSrvHeap = Other.mSrvHeap;
        mModelCache = std::move(Other.mModelCache);
        mLoadedMaterialJsonPaths = std::move(Other.mLoadedMaterialJsonPaths);
        mMaterials = std::move(Other.mMaterials);
        mPackedMaterials = std::move(Other.mPackedMaterials);
        mMaterialNameLookup = std::move(Other.mMaterialNameLookup);
        mMaterialTextureTable = std::move(Other.mMaterialTextureTable);
        mTextureTableLookup = std::move(Other.mTextureTableLookup);
        mTextureRecords = std::move(Other.mTextureRecords);
        mMaterialToTextureTableIndices = std::move(Other.mMaterialToTextureTableIndices);
        mTextureResidencyDecider = std::move(Other.mTextureResidencyDecider);
        mMaterialGroups = std::move(Other.mMaterialGroups);
        mMaterialGroupNameLookup = std::move(Other.mMaterialGroupNameLookup);
        mMaterialGroupSourcePaths = std::move(Other.mMaterialGroupSourcePaths);
        mMaterialGroupSourcePathLookup = std::move(Other.mMaterialGroupSourcePathLookup);
        mPipelines = std::move(Other.mPipelines);
        mPipelineLookup = std::move(Other.mPipelineLookup);

        Other.mDevice = nullptr;
        Other.mCopyQueue = nullptr;
        Other.mAllocator = nullptr;
        Other.mSrvHeap = nullptr;
        return *this;
    }

    void AssetRegistry::Initialize(ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator) {
        mDevice = Device;
        mCopyQueue = CopyQueue;
        mAllocator = Allocator;
    }

    void AssetRegistry::SetSrvHeap(Core::DX::DescriptorHeap* SrvHeap) {
        mSrvHeap = SrvHeap;
    }

    void AssetRegistry::SetTextureResidencyDecider(TextureResidencyDecider NewDecider) {
        mTextureResidencyDecider = std::move(NewDecider);
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
            const bool IsInitialized{ NewModel->InitializeFromModelResult(ModelData, mAllocator, mCopyQueue) };
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

    std::uint32_t AssetRegistry::AddMaterial(const asset::Material& MaterialData, const std::string& MaterialSourcePath) {
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
        NewMaterial.PackedData = BuildPackedMaterial(NewMaterial.Data, MaterialSourcePath);

        const std::uint32_t NewIndex{ static_cast<std::uint32_t>(mMaterials.size()) };
        mPackedMaterials.push_back(NewMaterial.PackedData);

        std::vector<std::uint32_t> TextureIndices{ BuildMaterialTextureTableIndices(NewMaterial.Data, NewMaterial.PackedData) };
        mMaterialToTextureTableIndices.push_back(std::move(TextureIndices));

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
                mMaterialGroupSourcePathLookup.insert_or_assign(SourcePath, ExistingIndex);
            }

            return ExistingIndex;
        }

        RegisteredMaterialGroup NewMaterialGroup{};
        NewMaterialGroup.Name = MaterialGroupName;
        NewMaterialGroup.Items.reserve(MaterialGroupData.Items.size());
        for (const asset::MaterialGroupItem& MaterialGroupItemData : MaterialGroupData.Items) {
            RegisteredMaterialGroupItem NewMaterialGroupItem{};
            NewMaterialGroupItem.MaterialIndex = AddMaterial(MaterialGroupItemData.MaterialData, SourcePath);
            NewMaterialGroupItem.Pipeline = ResolvePipelineByName(MaterialGroupItemData.PipelineName);
            NewMaterialGroup.Items.push_back(NewMaterialGroupItem);
        }

        const std::uint32_t NewIndex{ static_cast<std::uint32_t>(mMaterialGroups.size()) };
        mMaterialGroups.push_back(std::move(NewMaterialGroup));
        mMaterialGroupSourcePaths.push_back(SourcePath);
        if (SourcePath.empty() == false) {
            mMaterialGroupSourcePathLookup.insert_or_assign(SourcePath, NewIndex);
        }

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

    const std::vector<RFD::MaterialGpu>& AssetRegistry::GetPackedMaterials() const {
        return mPackedMaterials;
    }

    const std::vector<RFD::MaterialTextureTableItemGpu>& AssetRegistry::GetMaterialTextureTable() const {
        return mMaterialTextureTable;
    }

    std::uint32_t AssetRegistry::FindMaterialGroupIndexBySourcePath(const std::string& MaterialSourcePath) const {
        if (MaterialSourcePath.empty()) {
            return static_cast<std::uint32_t>(-1);
        }

        const auto FoundMaterialGroup{ mMaterialGroupSourcePathLookup.find(MaterialSourcePath) };
        if (FoundMaterialGroup == mMaterialGroupSourcePathLookup.end()) {
            return static_cast<std::uint32_t>(-1);
        }

        return FoundMaterialGroup->second;
    }

    std::uint32_t AssetRegistry::ResolveTextureTableIndex(const std::filesystem::path& TexturePath) {
        const std::filesystem::path NormalizedPath{ TexturePath.lexically_normal() };
        const std::string TextureKey{ NormalizedPath.generic_string() };
        const auto FoundTexture{ mTextureTableLookup.find(TextureKey) };
        if (FoundTexture != mTextureTableLookup.end()) {
            return FoundTexture->second;
        }

        RFD::MaterialTextureTableItemGpu TableItem{};
        TableItem.TextureSrvDescriptorIndex = std::numeric_limits<std::uint32_t>::max();

        Core::DX::TexPtr NewTexture{};
        if (mDevice != nullptr) {
            NewTexture = Core::DX::Texture::CreateFromFile(mDevice, NormalizedPath);
        }

        const std::uint32_t TextureTableIndex{ static_cast<std::uint32_t>(mMaterialTextureTable.size()) };
        mMaterialTextureTable.push_back(TableItem);
        mTextureTableLookup.insert_or_assign(TextureKey, TextureTableIndex);

        TextureRecord NewTextureData{};
        NewTextureData.Key = TextureKey;
        NewTextureData.Texture = std::move(NewTexture);
        NewTextureData.TableIndex = TextureTableIndex;
        NewTextureData.KeepResident = false;
        mTextureRecords.push_back(std::move(NewTextureData));
        return TextureTableIndex;
    }

    bool AssetRegistry::ShouldKeepTextureResident(const TextureRecord& TextureData, const std::unordered_set<std::uint32_t>& UsedTextureTableIndices) const {
        const bool IsUsedByDrawRecords{ UsedTextureTableIndices.find(TextureData.TableIndex) != UsedTextureTableIndices.end() };
        const bool IsLoaded{ TextureData.Texture != nullptr && TextureData.Texture->IsLoaded() == true };

        if (mTextureResidencyDecider) {
            return mTextureResidencyDecider(TextureData.TableIndex, IsUsedByDrawRecords, IsLoaded);
        }

        if (TextureData.KeepResident == true) {
            return true;
        }

        return IsUsedByDrawRecords;
    }

    void AssetRegistry::UpdateTextureTableItem(TextureRecord& TextureData) {
        if (TextureData.TableIndex >= mMaterialTextureTable.size()) {
            return;
        }

        RFD::MaterialTextureTableItemGpu& TextureTableItem{ mMaterialTextureTable[TextureData.TableIndex] };
        TextureTableItem.TextureSrvDescriptorIndex = std::numeric_limits<std::uint32_t>::max();

        if (TextureData.Texture == nullptr || TextureData.Texture->IsLoaded() == false) {
            return;
        }

        TextureData.Texture->CreateSRV(mDevice, *mSrvHeap);
        const D3D12_GPU_DESCRIPTOR_HANDLE TextureSrvHandle{ TextureData.Texture->GetSRV() };
        const D3D12_GPU_DESCRIPTOR_HANDLE HeapStartHandle{ mSrvHeap->GetHeap()->GetGPUDescriptorHandleForHeapStart() };
        const std::uint64_t DescriptorIncrement{ static_cast<std::uint64_t>(mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)) };
        if (TextureSrvHandle.ptr >= HeapStartHandle.ptr && DescriptorIncrement > 0) {
            TextureTableItem.TextureSrvDescriptorIndex = static_cast<std::uint32_t>((TextureSrvHandle.ptr - HeapStartHandle.ptr) / DescriptorIncrement);
        }
    }

    std::vector<std::uint32_t> AssetRegistry::BuildMaterialTextureTableIndices(const asset::Material& MaterialData, const RFD::MaterialGpu& PackedMaterial) const {
        std::unordered_set<std::uint32_t> UniqueTextureIndices{};

        for (const asset::MaterialProperty& PropertyData : MaterialData.Properties) {
            if (PropertyData.Data.GetKind() != asset::MaterialMapKind::String) {
                continue;
            }

            if (IsSupportedMaterialType(PropertyData.Type) == false) {
                continue;
            }

            const std::uint32_t TypeIndex{ static_cast<std::uint32_t>(PropertyData.Type) };
            const std::int64_t FieldIntValue{ PackedMaterial.Fields[TypeIndex].IntValue };
            if (FieldIntValue < 0) {
                continue;
            }

            const std::uint32_t TextureTableIndex{ static_cast<std::uint32_t>(FieldIntValue) };
            if (TextureTableIndex >= mMaterialTextureTable.size()) {
                continue;
            }

            UniqueTextureIndices.insert(TextureTableIndex);
        }

        std::vector<std::uint32_t> TextureIndices{};
        TextureIndices.reserve(UniqueTextureIndices.size());
        for (const std::uint32_t TextureTableIndex : UniqueTextureIndices) {
            TextureIndices.push_back(TextureTableIndex);
        }

        return TextureIndices;
    }

    void AssetRegistry::PrepareRenderTextures(const RFD::RenderFrameData& RenderData) {
        if (mDevice == nullptr || mCopyQueue == nullptr || mAllocator == nullptr || mSrvHeap == nullptr) {
            return;
        }

        std::unordered_set<std::uint32_t> UsedTextureTableIndices{};
        for (const RFD::DrawRecord& DrawRecordData : RenderData.drawRecords) {
            if (DrawRecordData.materialIndex >= mMaterialToTextureTableIndices.size()) {
                continue;
            }

            const std::vector<std::uint32_t>& MaterialTextureIndices{ mMaterialToTextureTableIndices[DrawRecordData.materialIndex] };
            for (const std::uint32_t TextureIndex : MaterialTextureIndices) {
                UsedTextureTableIndices.insert(TextureIndex);
            }
        }

        for (TextureRecord& TextureData : mTextureRecords) {
            if (TextureData.Texture == nullptr) {
                continue;
            }

            const bool ShouldLoadTexture{ ShouldKeepTextureResident(TextureData, UsedTextureTableIndices) };
            if (ShouldLoadTexture == true) {
                if (TextureData.Texture->IsLoaded() == false) {
                    const bool IsLoaded{ TextureData.Texture->Load(mCopyQueue, mAllocator) };
                    if (IsLoaded == true) {
                        UpdateTextureTableItem(TextureData);
                    }
                }
                continue;
            }

            if (TextureData.Texture->IsLoaded() == true) {
                TextureData.Texture->Unload();
                mMaterialTextureTable[TextureData.TableIndex].TextureSrvDescriptorIndex = std::numeric_limits<std::uint32_t>::max();
            }
        }
    }

    std::filesystem::path AssetRegistry::BuildTexturePathFromMaterialPath(const std::string& MaterialSourcePath, const std::string& TexturePath) const {
        const std::filesystem::path TexturePathValue{ TexturePath };
        const std::filesystem::path TextureFileName{ TexturePathValue.filename() };
        const std::filesystem::path DdsTextureFileName{ TextureFileName.stem().string() + std::string{ ".DDS" } };

        if (MaterialSourcePath.empty() == true) {
            return DdsTextureFileName;
        }

        const std::filesystem::path MaterialPath{ MaterialSourcePath };
        const std::filesystem::path SceneName{ MaterialPath.parent_path().filename() };
        if (SceneName.empty() == true) {
            return DdsTextureFileName;
        }

        return std::filesystem::path{ "Resources" } / SceneName / DdsTextureFileName;
    }

    std::int64_t AssetRegistry::ToMaterialIntValue(const asset::MaterialMap& MaterialMapData, const std::string& MaterialSourcePath) {
        if (MaterialMapData.GetKind() == asset::MaterialMapKind::Int) {
            return MaterialMapData.GetInt();
        }

        if (MaterialMapData.GetKind() == asset::MaterialMapKind::Bool) {
            return MaterialMapData.GetBool() == true ? 1 : 0;
        }

        if (MaterialMapData.GetKind() == asset::MaterialMapKind::String) {
            const std::filesystem::path TexturePath{ BuildTexturePathFromMaterialPath(MaterialSourcePath, MaterialMapData.GetString()) };
            const std::uint32_t TextureTableIndex{ ResolveTextureTableIndex(TexturePath) };
            return static_cast<std::int64_t>(TextureTableIndex);
        }

        return 0;
    }

    SimpleMath::Vector4 AssetRegistry::ToMaterialFloatValue(const asset::MaterialMap& MaterialMapData) {
        if (MaterialMapData.GetKind() == asset::MaterialMapKind::Real) {
            return SimpleMath::Vector4{ MaterialMapData.GetReal(), 0.0f, 0.0f, 0.0f };
        }

        if (MaterialMapData.GetKind() == asset::MaterialMapKind::Vec2) {
            const asset::Vec2 Vec2Value{ MaterialMapData.GetVec2() };
            return SimpleMath::Vector4{ Vec2Value.mX, Vec2Value.mY, 0.0f, 0.0f };
        }

        if (MaterialMapData.GetKind() == asset::MaterialMapKind::Vec3) {
            const asset::Vec3 Vec3Value{ MaterialMapData.GetVec3() };
            return SimpleMath::Vector4{ Vec3Value.mX, Vec3Value.mY, Vec3Value.mZ, 0.0f };
        }

        if (MaterialMapData.GetKind() == asset::MaterialMapKind::Vec4) {
            const asset::Vec4 Vec4Value{ MaterialMapData.GetVec4() };
            return SimpleMath::Vector4{ Vec4Value.mX, Vec4Value.mY, Vec4Value.mZ, Vec4Value.mW };
        }

        return SimpleMath::Vector4{};
    }

    RFD::MaterialGpu AssetRegistry::BuildPackedMaterial(const asset::Material& MaterialData, const std::string& MaterialSourcePath) {
        RFD::MaterialGpu PackedMaterial{};
        for (std::uint32_t FieldIndex{ 0 }; FieldIndex < RFD::MaterialGpu::FieldCount; ++FieldIndex) {
            PackedMaterial.Fields[FieldIndex].Type = FieldIndex;
        }

        for (const asset::MaterialProperty& PropertyData : MaterialData.Properties) {
            if (IsSupportedMaterialType(PropertyData.Type) == false) {
                continue;
            }

            const std::uint32_t TypeIndex{ static_cast<std::uint32_t>(PropertyData.Type) };
            PackedMaterial.Fields[TypeIndex].Type = TypeIndex;
            PackedMaterial.Fields[TypeIndex].FloatValue = ToMaterialFloatValue(PropertyData.Data);
            PackedMaterial.Fields[TypeIndex].IntValue = ToMaterialIntValue(PropertyData.Data, MaterialSourcePath);
        }

        return PackedMaterial;
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
