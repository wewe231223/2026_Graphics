#include "AssetRegistry.h"
#include <array>
#include <filesystem>
#include <limits>
#include <unordered_set>
#include "Asset/AssetBinaryReader.h"
#include "Asset/MaterialGroupJsonSerializer.h"
#include "PrimitiveModelFactory.h"

#ifdef max
#undef max
#endif

namespace {
    constexpr std::uint32_t MaterialFieldCount{ asset::MaterialTypeCount };

    bool IsSupportedMaterialType(asset::MaterialType MaterialTypeValue) {
        const std::uint32_t TypeValue{ static_cast<std::uint32_t>(MaterialTypeValue) };
        return TypeValue < MaterialFieldCount;
    }

    bool IsTextureMaterialType(asset::MaterialType MaterialTypeValue) {
        return MaterialTypeValue == asset::MaterialType::DiffuseTexture
            || MaterialTypeValue == asset::MaterialType::SpecularTexture
            || MaterialTypeValue == asset::MaterialType::AmbientTexture
            || MaterialTypeValue == asset::MaterialType::EmissiveTexture
            || MaterialTypeValue == asset::MaterialType::OpacityTexture
            || MaterialTypeValue == asset::MaterialType::ShininessTexture
            || MaterialTypeValue == asset::MaterialType::HeightBumpTexture
            || MaterialTypeValue == asset::MaterialType::NormalTexture
            || MaterialTypeValue == asset::MaterialType::DisplacementTexture
            || MaterialTypeValue == asset::MaterialType::ReflectionTexture
            || MaterialTypeValue == asset::MaterialType::LightmapTexture;
    }
}

namespace Game {
    AssetRegistry::AssetRegistry()
        : mDevice{ nullptr },
        mCopyQueue{ nullptr },
        mAllocator{ nullptr },
        mSrvHeap{ nullptr },
        mBackEnd{ std::make_shared<AssetRegistryBackEnd>() },
        mTextureResidencyDecider{} {
    }

    AssetRegistry::~AssetRegistry() {
    }

    AssetRegistry::AssetRegistry(AssetRegistry&& Other) noexcept
        : mDevice{ Other.mDevice },
        mCopyQueue{ Other.mCopyQueue },
        mAllocator{ Other.mAllocator },
        mSrvHeap{ Other.mSrvHeap },
        mBackEnd{ std::move(Other.mBackEnd) },
        mTextureResidencyDecider{ std::move(Other.mTextureResidencyDecider) } {
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
        mBackEnd = std::move(Other.mBackEnd);
        mTextureResidencyDecider = std::move(Other.mTextureResidencyDecider);

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
        const bool IsDefaultMaterialLoaded{ LoadMaterialGroups(std::string{ "Resources/DefaultResource/DefaultMaterial.json" }) };
        if (IsDefaultMaterialLoaded == false) {
            asset::MaterialGroup DefaultMaterialGroup{};
            DefaultMaterialGroup.Name = "DefaultMaterial";

            asset::MaterialGroupItem DefaultMaterialGroupItem{};
            DefaultMaterialGroupItem.PipelineName = "DefaultGraphics";
            DefaultMaterialGroupItem.MaterialData.Name = "Default";
            DefaultMaterialGroupItem.MaterialData.PBR = false;
            DefaultMaterialGroup.Items.push_back(DefaultMaterialGroupItem);

            AddMaterialGroup(DefaultMaterialGroup);
        }
    }

    void AssetRegistry::SetSrvHeap(Interface::IDescriptorHeap* SrvHeap) {
        mSrvHeap = SrvHeap;
    }

    void AssetRegistry::SetBackEnd(const std::shared_ptr<IAssetRegistryBackEnd>& NewBackEnd) {
        if (NewBackEnd == nullptr) {
            return;
        }

        mBackEnd = NewBackEnd;
    }

    std::shared_ptr<IAssetRegistryBackEnd> AssetRegistry::GetBackEnd() const {
        return mBackEnd;
    }

    void AssetRegistry::SetTextureResidencyDecider(TextureResidencyDecider NewDecider) {
        mTextureResidencyDecider = std::move(NewDecider);
    }

    std::shared_ptr<Model> AssetRegistry::GetModel(const std::string& ModelBinaryPath) {
        IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        AssetRegistryStorage& Storage{ BackEnd->GetStorage() };
        auto& ModelBucket{ Storage.GetBucket<ModelBucketTag>() };

        const auto FoundModel{ ModelBucket.mNameLookup.find(ModelBinaryPath) };
        if (FoundModel != ModelBucket.mNameLookup.end() && FoundModel->second < ModelBucket.mAssets.size()) {
            return ModelBucket.mAssets[FoundModel->second];
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

        const std::uint32_t NewIndex{ static_cast<std::uint32_t>(ModelBucket.mAssets.size()) };
        ModelBucket.mAssets.push_back(NewModel);
        ModelBucket.mNameLookup.insert_or_assign(ModelBinaryPath, NewIndex);
        return NewModel;
    }

    bool AssetRegistry::LoadMaterialGroups(const std::string& MaterialJsonPath) {
        IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        AssetRegistryStorage& Storage{ BackEnd->GetStorage() };
        std::unordered_set<std::string>& LoadedMaterialJsonPaths{ Storage.GetLoadedMaterialJsonPaths() };
        if (LoadedMaterialJsonPaths.find(MaterialJsonPath) != LoadedMaterialJsonPaths.end()) {
            return true;
        }

        std::vector<asset::MaterialGroup> MaterialGroups{};
        if (ReadMaterialGroups(MaterialJsonPath, MaterialGroups) == false) {
            return false;
        }

        for (const asset::MaterialGroup& MaterialGroupData : MaterialGroups) {
            AddMaterialGroupWithSource(MaterialGroupData, MaterialJsonPath);
        }

        LoadedMaterialJsonPaths.insert(MaterialJsonPath);
        return true;
    }

    std::uint32_t AssetRegistry::AddMaterial(const asset::Material& MaterialData, const std::string& MaterialSourcePath) {
        IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        AssetRegistryStorage& Storage{ BackEnd->GetStorage() };
        auto& MaterialBucket{ Storage.GetBucket<MaterialBucketTag>() };

        std::string MaterialName{ MaterialData.Name };
        if (MaterialName.empty()) {
            MaterialName = std::string{ "Material_" } + std::to_string(MaterialBucket.mAssets.size());
        }

        const auto FoundMaterial{ MaterialBucket.mNameLookup.find(MaterialName) };
        if (FoundMaterial != MaterialBucket.mNameLookup.end()) {
            return FoundMaterial->second;
        }

        RegisteredMaterial NewMaterial{};
        NewMaterial.Name = MaterialName;
        NewMaterial.Data = MaterialData;
        NewMaterial.Data.Name = MaterialName;
        NewMaterial.PackedData = BuildPackedMaterial(NewMaterial.Data, MaterialSourcePath);

        const std::uint32_t NewIndex{ static_cast<std::uint32_t>(MaterialBucket.mAssets.size()) };
        MaterialBucket.mGpuData.push_back(NewMaterial.PackedData);

        std::vector<std::uint32_t> TextureIndices{ BuildMaterialTextureTableIndices(NewMaterial.Data, NewMaterial.PackedData) };
        Storage.GetMaterialToTextureTableIndices().push_back(std::move(TextureIndices));

        MaterialBucket.mAssets.push_back(std::move(NewMaterial));
        MaterialBucket.mNameLookup.insert_or_assign(MaterialBucket.mAssets.back().Name, NewIndex);
        return NewIndex;
    }

    std::uint32_t AssetRegistry::AddMaterialGroup(const asset::MaterialGroup& MaterialGroupData) {
        return AddMaterialGroupWithSource(MaterialGroupData, std::string{});
    }

    std::uint32_t AssetRegistry::AddMaterialGroupWithSource(const asset::MaterialGroup& MaterialGroupData, const std::string& SourcePath) {
        IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        AssetRegistryStorage& Storage{ BackEnd->GetStorage() };
        auto& MaterialGroupBucket{ Storage.GetBucket<MaterialGroupBucketTag>() };
        std::vector<std::string>& MaterialGroupSourcePaths{ Storage.GetMaterialGroupSourcePaths() };
        std::unordered_map<std::string, std::uint32_t>& MaterialGroupSourcePathLookup{ Storage.GetMaterialGroupSourcePathLookup() };

        std::string MaterialGroupName{ MaterialGroupData.Name };
        if (MaterialGroupName.empty()) {
            MaterialGroupName = std::string{ "MaterialGroup_" } + std::to_string(MaterialGroupBucket.mAssets.size());
        }

        const auto FoundMaterialGroup{ MaterialGroupBucket.mNameLookup.find(MaterialGroupName) };
        if (FoundMaterialGroup != MaterialGroupBucket.mNameLookup.end()) {
            const std::uint32_t ExistingIndex{ FoundMaterialGroup->second };
            if (SourcePath.empty() == false && ExistingIndex < MaterialGroupSourcePaths.size() && MaterialGroupSourcePaths[ExistingIndex].empty()) {
                MaterialGroupSourcePaths[ExistingIndex] = SourcePath;
                MaterialGroupSourcePathLookup.insert_or_assign(SourcePath, ExistingIndex);
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

        const std::uint32_t NewIndex{ static_cast<std::uint32_t>(MaterialGroupBucket.mAssets.size()) };
        MaterialGroupBucket.mAssets.push_back(std::move(NewMaterialGroup));
        MaterialGroupSourcePaths.push_back(SourcePath);
        if (SourcePath.empty() == false) {
            MaterialGroupSourcePathLookup.insert_or_assign(SourcePath, NewIndex);
        }

        MaterialGroupBucket.mNameLookup.insert_or_assign(MaterialGroupBucket.mAssets.back().Name, NewIndex);
        return NewIndex;
    }

    std::uint32_t AssetRegistry::FindMaterialIndexByName(const std::string& MaterialName) const {
        const IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        const AssetRegistryStorage& Storage{ BackEnd->GetStorage() };
        const auto& MaterialBucket{ Storage.GetBucket<MaterialBucketTag>() };
        const auto FoundMaterial{ MaterialBucket.mNameLookup.find(MaterialName) };
        if (FoundMaterial == MaterialBucket.mNameLookup.end()) {
            return static_cast<std::uint32_t>(-1);
        }

        return FoundMaterial->second;
    }

    const std::vector<RegisteredMaterial>& AssetRegistry::GetMaterials() const {
        const IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        return BackEnd->GetStorage().GetBucket<MaterialBucketTag>().mAssets;
    }

    const std::vector<RegisteredMaterialGroup>& AssetRegistry::GetMaterialGroups() const {
        const IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        return BackEnd->GetStorage().GetBucket<MaterialGroupBucketTag>().mAssets;
    }

    const std::vector<RFD::MaterialGpu>& AssetRegistry::GetPackedMaterials() const {
        const IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        return BackEnd->GetStorage().GetBucket<MaterialBucketTag>().mGpuData;
    }

    const std::vector<RFD::MaterialTextureTableItemGpu>& AssetRegistry::GetMaterialTextureTable() const {
        const IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        return BackEnd->GetStorage().GetBucket<TextureTableBucketTag>().mAssets;
    }

    std::uint32_t AssetRegistry::FindMaterialGroupIndexBySourcePath(const std::string& MaterialSourcePath) const {
        if (MaterialSourcePath.empty()) {
            return static_cast<std::uint32_t>(-1);
        }

        const IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        const AssetRegistryStorage& Storage{ BackEnd->GetStorage() };
        const auto& MaterialGroupSourcePathLookup{ Storage.GetMaterialGroupSourcePathLookup() };
        const auto FoundMaterialGroup{ MaterialGroupSourcePathLookup.find(MaterialSourcePath) };
        if (FoundMaterialGroup == MaterialGroupSourcePathLookup.end()) {
            return static_cast<std::uint32_t>(-1);
        }

        return FoundMaterialGroup->second;
    }

    std::string AssetRegistry::FindModelSelectorByPointer(const Model* ModelPointer) const {
        if (ModelPointer == nullptr) {
            return std::string{};
        }

        const IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        const AssetRegistryStorage& Storage{ BackEnd->GetStorage() };
        const auto& ModelBucket{ Storage.GetBucket<ModelBucketTag>() };
        for (const auto& Pair : ModelBucket.mNameLookup) {
            const std::uint32_t ModelIndex{ Pair.second };
            if (ModelIndex >= ModelBucket.mAssets.size()) {
                continue;
            }

            const std::shared_ptr<Model>& RegisteredModel{ ModelBucket.mAssets[ModelIndex] };
            if (RegisteredModel.get() == ModelPointer) {
                return Pair.first;
            }
        }

        return std::string{};
    }

    std::string AssetRegistry::FindMaterialGroupSourcePathByIndex(std::uint32_t MaterialGroupIndex) const {
        const IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        const AssetRegistryStorage& Storage{ BackEnd->GetStorage() };
        const std::vector<std::string>& MaterialGroupSourcePaths{ Storage.GetMaterialGroupSourcePaths() };
        if (MaterialGroupIndex >= MaterialGroupSourcePaths.size()) {
            return std::string{};
        }

        return MaterialGroupSourcePaths[MaterialGroupIndex];
    }

    Interface::IPipeline* AssetRegistry::GetPipelineByName(const std::string& PipelineName) {
        return ResolvePipelineByName(PipelineName);
    }

    std::uint32_t AssetRegistry::ResolveTextureTableIndex(const std::filesystem::path& TexturePath) {
        IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        AssetRegistryStorage& Storage{ BackEnd->GetStorage() };
        auto& TextureTableBucket{ Storage.GetBucket<TextureTableBucketTag>() };
        std::vector<AssetRegistryTextureRecord>& TextureRecords{ Storage.GetTextureRecords() };

        const std::filesystem::path NormalizedPath{ TexturePath.lexically_normal() };
        const std::string TextureKey{ NormalizedPath.generic_string() };
        const auto FoundTexture{ TextureTableBucket.mNameLookup.find(TextureKey) };
        if (FoundTexture != TextureTableBucket.mNameLookup.end()) {
            return FoundTexture->second;
        }

        RFD::MaterialTextureTableItemGpu TableItem{};
        TableItem.TextureSrvDescriptorIndex = std::numeric_limits<std::uint32_t>::max();

        Core::DX::TexPtr NewTexture{};
        if (mDevice != nullptr) {
            NewTexture = Core::DX::Texture::CreateFromFile(mDevice, NormalizedPath);
        }

        const std::uint32_t TextureTableIndex{ static_cast<std::uint32_t>(TextureTableBucket.mAssets.size()) };
        TextureTableBucket.mAssets.push_back(TableItem);
        TextureTableBucket.mNameLookup.insert_or_assign(TextureKey, TextureTableIndex);

        AssetRegistryTextureRecord NewTextureData{};
        NewTextureData.Key = TextureKey;
        NewTextureData.Texture = std::move(NewTexture);
        NewTextureData.TableIndex = TextureTableIndex;
        NewTextureData.KeepResident = false;
        TextureRecords.push_back(std::move(NewTextureData));
        return TextureTableIndex;
    }

    bool AssetRegistry::ShouldKeepTextureResident(const AssetRegistryTextureRecord& TextureData, const std::unordered_set<std::uint32_t>& UsedTextureTableIndices) const {
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

    void AssetRegistry::UpdateTextureTableItem(AssetRegistryTextureRecord& TextureData) {
        IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        auto& TextureTableBucket{ BackEnd->GetStorage().GetBucket<TextureTableBucketTag>() };

        if (TextureData.TableIndex >= TextureTableBucket.mAssets.size()) {
            return;
        }

        RFD::MaterialTextureTableItemGpu& TextureTableItem{ TextureTableBucket.mAssets[TextureData.TableIndex] };
        TextureTableItem.TextureSrvDescriptorIndex = std::numeric_limits<std::uint32_t>::max();

        if (TextureData.Texture == nullptr || TextureData.Texture->IsLoaded() == false) {
            return;
        }

        TextureData.Texture->CreateSRV(mDevice, mSrvHeap);
        const D3D12_GPU_DESCRIPTOR_HANDLE TextureSrvHandle{ TextureData.Texture->GetSRV() };
        const D3D12_GPU_DESCRIPTOR_HANDLE HeapStartHandle{ mSrvHeap->GetHeap()->GetGPUDescriptorHandleForHeapStart() };
        const std::uint64_t DescriptorIncrement{ static_cast<std::uint64_t>(mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)) };
        if (TextureSrvHandle.ptr >= HeapStartHandle.ptr && DescriptorIncrement > 0) {
            TextureTableItem.TextureSrvDescriptorIndex = static_cast<std::uint32_t>((TextureSrvHandle.ptr - HeapStartHandle.ptr) / DescriptorIncrement);
        }
    }

    std::vector<std::uint32_t> AssetRegistry::BuildMaterialTextureTableIndices(const asset::Material& MaterialData, const RFD::MaterialGpu& PackedMaterial) const {
        const IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        auto& TextureTableBucket{ BackEnd->GetStorage().GetBucket<TextureTableBucketTag>() };
        std::unordered_set<std::uint32_t> UniqueTextureIndices{};

        for (const asset::MaterialProperty& PropertyData : MaterialData.Properties) {
            if (PropertyData.Data.GetKind() != asset::MaterialMapKind::String) {
                continue;
            }

            if (IsSupportedMaterialType(PropertyData.Type) == false || IsTextureMaterialType(PropertyData.Type) == false) {
                continue;
            }

            const std::uint32_t TypeIndex{ static_cast<std::uint32_t>(PropertyData.Type) };
            const std::int64_t FieldIntValue{ PackedMaterial.Fields[TypeIndex].IntValue };
            if (FieldIntValue < 0) {
                continue;
            }

            const std::uint32_t TextureTableIndex{ static_cast<std::uint32_t>(FieldIntValue) };
            if (TextureTableIndex >= TextureTableBucket.mAssets.size()) {
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

        IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        AssetRegistryStorage& Storage{ BackEnd->GetStorage() };
        std::vector<std::vector<std::uint32_t>>& MaterialToTextureTableIndices{ Storage.GetMaterialToTextureTableIndices() };
        std::vector<AssetRegistryTextureRecord>& TextureRecords{ Storage.GetTextureRecords() };
        auto& TextureTableBucket{ Storage.GetBucket<TextureTableBucketTag>() };

        std::unordered_set<std::uint32_t> UsedTextureTableIndices{};
        for (const RFD::DrawRecord& DrawRecordData : RenderData.drawRecords) {
            if (DrawRecordData.materialIndex >= MaterialToTextureTableIndices.size()) {
                continue;
            }

            const std::vector<std::uint32_t>& MaterialTextureIndices{ MaterialToTextureTableIndices[DrawRecordData.materialIndex] };
            for (const std::uint32_t TextureIndex : MaterialTextureIndices) {
                UsedTextureTableIndices.insert(TextureIndex);
            }
        }

        for (AssetRegistryTextureRecord& TextureData : TextureRecords) {
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
                TextureTableBucket.mAssets[TextureData.TableIndex].TextureSrvDescriptorIndex = std::numeric_limits<std::uint32_t>::max();
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

    std::int64_t AssetRegistry::ToMaterialIntValue(asset::MaterialType MaterialTypeValue, const asset::MaterialMap& MaterialMapData, const std::string& MaterialSourcePath) {
        if (IsTextureMaterialType(MaterialTypeValue) == true) {
            if (MaterialMapData.GetKind() == asset::MaterialMapKind::String) {
                const std::filesystem::path TexturePath{ BuildTexturePathFromMaterialPath(MaterialSourcePath, MaterialMapData.GetString()) };
                const std::uint32_t TextureTableIndex{ ResolveTextureTableIndex(TexturePath) };
                return static_cast<std::int64_t>(TextureTableIndex);
            }

            if (MaterialMapData.GetKind() == asset::MaterialMapKind::Int) {
                return MaterialMapData.GetInt();
            }

            if (MaterialMapData.GetKind() == asset::MaterialMapKind::Bool) {
                return MaterialMapData.GetBool() == true ? 1 : 0;
            }

            return static_cast<std::int64_t>(-1);
        }

        if (MaterialMapData.GetKind() == asset::MaterialMapKind::Int) {
            return MaterialMapData.GetInt();
        }

        if (MaterialMapData.GetKind() == asset::MaterialMapKind::Bool) {
            return MaterialMapData.GetBool() == true ? 1 : 0;
        }

        return 0;
    }

    SimpleMath::Vector4 AssetRegistry::ToMaterialFloatValue(const asset::MaterialMap& MaterialMapData) {
        if (MaterialMapData.GetKind() == asset::MaterialMapKind::Real) {
            return SimpleMath::Vector4{ MaterialMapData.GetReal(), 0.0f, 0.0f, 0.0f };
        }

        if (MaterialMapData.GetKind() == asset::MaterialMapKind::Vec2) {
            const asset::Vec2 Vec2Value{ MaterialMapData.GetVec2() };
            return SimpleMath::Vector4{ Vec2Value.x, Vec2Value.y, 0.0f, 0.0f };
        }

        if (MaterialMapData.GetKind() == asset::MaterialMapKind::Vec3) {
            const asset::Vec3 Vec3Value{ MaterialMapData.GetVec3() };
            return SimpleMath::Vector4{ Vec3Value.x, Vec3Value.y, Vec3Value.z, 0.0f };
        }

        if (MaterialMapData.GetKind() == asset::MaterialMapKind::Vec4) {
            const asset::Vec4 Vec4Value{ MaterialMapData.GetVec4() };
            return SimpleMath::Vector4{ Vec4Value.x, Vec4Value.y, Vec4Value.z, Vec4Value.w };
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
            PackedMaterial.Fields[TypeIndex].IntValue = ToMaterialIntValue(PropertyData.Type, PropertyData.Data, MaterialSourcePath);
        }

        return PackedMaterial;
    }

    Interface::IPipeline* AssetRegistry::ResolvePipelineByName(const std::string& PipelineName) {
        if (PipelineName.empty()) {
            return nullptr;
        }

        IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        AssetRegistryStorage& Storage{ BackEnd->GetStorage() };
        std::unordered_map<std::string, Interface::IPipeline*>& PipelineLookup{ Storage.GetPipelineLookup() };
        const auto FoundPipeline{ PipelineLookup.find(PipelineName) };
        if (FoundPipeline != PipelineLookup.end()) {
            return FoundPipeline->second;
        }

        std::unique_ptr<Base::Pipeline> NewPipeline{ std::make_unique<Base::Pipeline>() };
        if (NewPipeline->Initialize(PipelineName) == false) {
            PipelineLookup.insert_or_assign(PipelineName, nullptr);
            return nullptr;
        }

        Interface::IPipeline* PipelinePointer{ NewPipeline.get() };
        std::vector<std::unique_ptr<Base::Pipeline>>& Pipelines{ Storage.GetPipelines() };
        Pipelines.push_back(std::move(NewPipeline));
        PipelineLookup.insert_or_assign(PipelineName, PipelinePointer);
        return PipelinePointer;
    }

    bool AssetRegistry::ReadModelData(const std::string& ModelBinaryPath, asset::ModelResult& OutModelData) const {
        if (PrimitiveModelFactory::TryCreateModelResult(ModelBinaryPath, OutModelData)) {
            return true;
        }

        asset::AssetBinaryReader Reader{};
        return Reader.ReadFromFile(ModelBinaryPath, OutModelData);
    }

    bool AssetRegistry::ReadMaterialGroups(const std::string& MaterialJsonPath, std::vector<asset::MaterialGroup>& OutMaterialGroups) const {
        asset::MaterialGroupJsonSerializer Serializer{};
        return Serializer.ReadFromFile(MaterialJsonPath, OutMaterialGroups);
    }
}
