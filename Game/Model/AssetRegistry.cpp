#include "AssetRegistry.h"
#include <array>
#include <bit>
#include <filesystem>
#include <limits>
#include <unordered_set>
#include <unordered_map>
#include <string_view>
#include <sstream>
#include <fstream>
#include <utility>
#include <ryml_std.hpp>
#include <ryml.hpp>
#include "Asset/AssetBinaryReader.h"
#include "Asset/AnimationBinaryReader.h"
#include "Asset/MaterialGroupJsonSerializer.h"
#include "PrimitiveModelFactory.h"
#include "TerrainHeightFieldFactory.h"
#include "TerrainRenderResource.h"
#include "TerrainTiledMeshBuilder.h"
#include "Game/Terrain/TerrainManager.h"
#include "Utility/ErrorHandler.h"

namespace {
    constexpr std::uint32_t MaterialFieldCount{ asset::MaterialTypeCount };
    constexpr std::uint64_t TerrainKeyHashOffset{ 14695981039346656037ULL };
    constexpr std::uint64_t TerrainKeyHashPrime{ 1099511628211ULL };

    bool IsSupportedMaterialType(asset::MaterialType MaterialTypeValue) {
        const std::uint32_t TypeValue{ static_cast<std::uint32_t>(MaterialTypeValue) };
        return TypeValue < MaterialFieldCount;
    }

    bool IsTextureMaterialType(asset::MaterialType MaterialTypeValue) {
        const std::uint32_t TypeValue{ static_cast<std::uint32_t>(MaterialTypeValue) };
        const std::uint32_t TerrainSplatTextureStart{ static_cast<std::uint32_t>(asset::MaterialType::TerrainSplatTexture0) };
        const std::uint32_t TerrainSplatTextureEnd{ static_cast<std::uint32_t>(asset::MaterialType::TerrainSplatTexture3) };
        const std::uint32_t TerrainDiffuseTextureStart{ static_cast<std::uint32_t>(asset::MaterialType::TerrainDiffuseTexture0) };
        const std::uint32_t TerrainDiffuseTextureEnd{ static_cast<std::uint32_t>(asset::MaterialType::TerrainDiffuseTexture15) };
        const std::uint32_t TerrainNormalTextureStart{ static_cast<std::uint32_t>(asset::MaterialType::TerrainNormalTexture0) };
        const std::uint32_t TerrainNormalTextureEnd{ static_cast<std::uint32_t>(asset::MaterialType::TerrainNormalTexture15) };

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
            || MaterialTypeValue == asset::MaterialType::LightmapTexture
            || (TypeValue >= TerrainSplatTextureStart && TypeValue <= TerrainSplatTextureEnd)
            || (TypeValue >= TerrainDiffuseTextureStart && TypeValue <= TerrainDiffuseTextureEnd)
            || (TypeValue >= TerrainNormalTextureStart && TypeValue <= TerrainNormalTextureEnd);
    }

    std::string BuildTerrainHeightSourceTypeText(Game::TerrainHeightSourceType SourceType) {
        if (SourceType == Game::TerrainHeightSourceType::Procedural) {
            return "Procedural";
        }

        return "HeightMap";
    }

    void AppendTerrainKeyHashByte(std::uint64_t& Hash, std::uint8_t Value) {
        Hash ^= static_cast<std::uint64_t>(Value);
        Hash *= TerrainKeyHashPrime;
    }

    void AppendTerrainKeyHashUInt32(std::uint64_t& Hash, std::uint32_t Value) {
        for (std::uint32_t Shift{ 0 }; Shift < 32; Shift += 8) {
            AppendTerrainKeyHashByte(Hash, static_cast<std::uint8_t>((Value >> Shift) & 0xffu));
        }
    }

    void AppendTerrainKeyHashFloat(std::uint64_t& Hash, float Value) {
        const std::uint32_t Bits{ std::bit_cast<std::uint32_t>(Value) };
        AppendTerrainKeyHashUInt32(Hash, Bits);
    }

    void AppendTerrainKeyHashBool(std::uint64_t& Hash, bool Value) {
        AppendTerrainKeyHashByte(Hash, Value == true ? static_cast<std::uint8_t>(1) : static_cast<std::uint8_t>(0));
    }

    void AppendTerrainKeyHashString(std::uint64_t& Hash, const std::string& Value) {
        AppendTerrainKeyHashUInt32(Hash, static_cast<std::uint32_t>(Value.size()));
        for (const char Character : Value) {
            AppendTerrainKeyHashByte(Hash, static_cast<std::uint8_t>(Character));
        }
    }

    void AppendTerrainKeyHashFloatVector(std::uint64_t& Hash, const std::vector<float>& Values) {
        AppendTerrainKeyHashUInt32(Hash, static_cast<std::uint32_t>(Values.size()));
        for (const float Value : Values) {
            AppendTerrainKeyHashFloat(Hash, Value);
        }
    }

    std::uint64_t BuildTerrainStringHash(const std::string& Value) {
        std::uint64_t Hash{ TerrainKeyHashOffset };
        AppendTerrainKeyHashString(Hash, Value);
        return Hash;
    }

    void AppendTerrainKeyHashSplatMapDesc(std::uint64_t& Hash, const Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapDesc& Desc) {
        AppendTerrainKeyHashUInt32(Hash, static_cast<std::uint32_t>(Desc.mVariables.size()));
        for (const Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapVariableDesc& VariableDesc : Desc.mVariables) {
            AppendTerrainKeyHashString(Hash, VariableDesc.mName);
            AppendTerrainKeyHashString(Hash, VariableDesc.mFormula);
        }

        AppendTerrainKeyHashUInt32(Hash, static_cast<std::uint32_t>(Desc.mLayers.size()));
        for (const Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapLayerDesc& LayerDesc : Desc.mLayers) {
            AppendTerrainKeyHashString(Hash, LayerDesc.mName);
            AppendTerrainKeyHashString(Hash, LayerDesc.mFormula);
        }

        AppendTerrainKeyHashUInt32(Hash, Desc.mFallbackLayerIndex);
        AppendTerrainKeyHashBool(Hash, Desc.mNormalizeWeights);
        AppendTerrainKeyHashFloat(Hash, Desc.mMinimumWeightSum);
    }

    std::uint64_t BuildTerrainProceduralHeightFieldDescHash(const Game::TerrainProceduralHeightFieldDesc& Desc) {
        std::uint64_t Hash{ TerrainKeyHashOffset };
        AppendTerrainKeyHashUInt32(Hash, Desc.mWidth);
        AppendTerrainKeyHashUInt32(Hash, Desc.mHeight);
        AppendTerrainKeyHashUInt32(Hash, Desc.mSeed);
        AppendTerrainKeyHashBool(Hash, Desc.mUseRandomSeed);
        AppendTerrainKeyHashBool(Hash, Desc.mHasResolvedRandomSeed);
        AppendTerrainKeyHashUInt32(Hash, Desc.mOctaveCount);
        AppendTerrainKeyHashFloat(Hash, Desc.mNoiseScale);
        AppendTerrainKeyHashFloat(Hash, Desc.mPersistence);
        AppendTerrainKeyHashFloat(Hash, Desc.mLacunarity);
        AppendTerrainKeyHashFloat(Hash, Desc.mBaseHeight);
        AppendTerrainKeyHashFloat(Hash, Desc.mHeightAmplitude);
        AppendTerrainKeyHashFloat(Hash, Desc.mLodExponent);
        AppendTerrainKeyHashUInt32(Hash, Desc.mSmoothingPassCount);
        AppendTerrainKeyHashUInt32(Hash, Desc.mMinimumWidth);
        AppendTerrainKeyHashUInt32(Hash, Desc.mMinimumHeight);
        AppendTerrainKeyHashUInt32(Hash, Desc.mMaximumOctaveCount);
        AppendTerrainKeyHashUInt32(Hash, Desc.mMaximumSmoothingPassCount);
        AppendTerrainKeyHashFloat(Hash, Desc.mMinimumHeightValue);
        AppendTerrainKeyHashFloat(Hash, Desc.mMaximumHeightValue);
        AppendTerrainKeyHashFloat(Hash, Desc.mSampleScaleX);
        AppendTerrainKeyHashFloat(Hash, Desc.mSampleScaleZ);
        AppendTerrainKeyHashUInt32(Hash, static_cast<std::uint32_t>(Desc.mSampleOffsetX));
        AppendTerrainKeyHashUInt32(Hash, static_cast<std::uint32_t>(Desc.mSampleOffsetZ));
        AppendTerrainKeyHashFloat(Hash, Desc.mInitialFrequency);
        AppendTerrainKeyHashFloat(Hash, Desc.mInitialAmplitude);
        AppendTerrainKeyHashUInt32(Hash, Desc.mOctaveSeedStep);
        AppendTerrainKeyHashFloat(Hash, Desc.mNoiseNormalizationScale);
        AppendTerrainKeyHashFloat(Hash, Desc.mNoiseNormalizationBias);
        AppendTerrainKeyHashUInt32(Hash, Desc.mHashShiftA);
        AppendTerrainKeyHashUInt32(Hash, Desc.mHashShiftB);
        AppendTerrainKeyHashUInt32(Hash, Desc.mHashShiftC);
        AppendTerrainKeyHashUInt32(Hash, Desc.mHashShiftLimitExclusive);
        AppendTerrainKeyHashUInt32(Hash, Desc.mHashMultiplierA);
        AppendTerrainKeyHashUInt32(Hash, Desc.mHashMultiplierB);
        AppendTerrainKeyHashUInt32(Hash, Desc.mHashCoordinateOffsetX);
        AppendTerrainKeyHashUInt32(Hash, Desc.mHashCoordinateOffsetZ);
        AppendTerrainKeyHashUInt32(Hash, Desc.mGradientDirectionCount);
        AppendTerrainKeyHashFloat(Hash, Desc.mFadeCoefficientA);
        AppendTerrainKeyHashFloat(Hash, Desc.mFadeCoefficientB);
        AppendTerrainKeyHashFloat(Hash, Desc.mFadeCoefficientC);
        AppendTerrainKeyHashFloat(Hash, Desc.mSmoothingCornerWeight);
        AppendTerrainKeyHashFloat(Hash, Desc.mSmoothingEdgeWeight);
        AppendTerrainKeyHashFloat(Hash, Desc.mSmoothingCenterWeight);
        AppendTerrainKeyHashFloat(Hash, Desc.mSmoothingWeightSum);
        AppendTerrainKeyHashSplatMapDesc(Hash, Desc.mSplatMapDesc);
        return Hash;
    }

    std::uint64_t BuildTerrainMeshDescHash(const Game::TerrainBuildDesc& Desc) {
        std::uint64_t Hash{ TerrainKeyHashOffset };
        AppendTerrainKeyHashFloat(Hash, Desc.MaxHeight);
        AppendTerrainKeyHashFloat(Hash, Desc.CellSizeX);
        AppendTerrainKeyHashFloat(Hash, Desc.CellSizeZ);
        AppendTerrainKeyHashBool(Hash, Desc.FlipV);
        AppendTerrainKeyHashBool(Hash, Desc.CenterOrigin);
        AppendTerrainKeyHashUInt32(Hash, Desc.TileQuadCount);
        AppendTerrainKeyHashUInt32(Hash, Desc.LodCount);
        AppendTerrainKeyHashFloatVector(Hash, Desc.LodDistances);
        AppendTerrainKeyHashBool(Hash, Desc.mStreamingEnabled);
        AppendTerrainKeyHashUInt32(Hash, Desc.mStreamingGridStep);
        return Hash;
    }

    std::string BuildTerrainKeyHashText(std::uint64_t Hash) {
        return std::to_string(Hash);
    }

    std::string BuildTerrainRenderResourceKey(const Game::TerrainBuildDesc& Desc) {
        std::string Key{ "terrain:" };
        Key += std::string{ "HeightSourceType=" } + BuildTerrainHeightSourceTypeText(Desc.mHeightSourceType);
        if (Desc.mHeightSourceType == Game::TerrainHeightSourceType::HeightMap) {
            Key += std::string{ ";HeightMapPathHash=" } + BuildTerrainKeyHashText(BuildTerrainStringHash(Desc.HeightMapPath));
        }
        else if (Desc.mProceduralHeightFieldPath.empty() == false) {
            Key += std::string{ ";ProceduralHeightFieldPathHash=" } + BuildTerrainKeyHashText(BuildTerrainStringHash(Desc.mProceduralHeightFieldPath));
            Key += std::string{ ";ProceduralDescHash=" } + BuildTerrainKeyHashText(BuildTerrainProceduralHeightFieldDescHash(Desc.mProceduralHeightFieldDesc));
        }
        else {
            Key += std::string{ ";ProceduralDescHash=" } + BuildTerrainKeyHashText(BuildTerrainProceduralHeightFieldDescHash(Desc.mProceduralHeightFieldDesc));
        }

        Key += std::string{ ";MeshDescHash=" } + BuildTerrainKeyHashText(BuildTerrainMeshDescHash(Desc));

        return Key;
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

    std::shared_ptr<TerrainRenderResource> AssetRegistry::GetTerrainRenderResource(const TerrainBuildDesc& Desc) {
        IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        AssetRegistryStorage& Storage{ BackEnd->GetStorage() };
        auto& TerrainBucket{ Storage.GetBucket<TerrainRenderResourceBucketTag>() };
        TerrainBuildDesc ResolvedDesc{ Desc };
        try {
            TerrainHeightFieldFactory HeightFieldFactory{};
            if (ResolvedDesc.mHeightSourceType == TerrainHeightSourceType::Procedural) {
                ResolvedDesc.mProceduralHeightFieldDesc = HeightFieldFactory.ResolveProceduralHeightFieldDesc(ResolvedDesc);
            }
        }
        catch (const std::exception& Exception) {
            ErrorHandler::report("TerrainRenderResource", Exception.what(), ErrorHandler::Level::Warning);
            return nullptr;
        }

        const std::string TerrainKey{ BuildTerrainRenderResourceKey(ResolvedDesc) };

        const auto FoundTerrain{ TerrainBucket.mNameLookup.find(TerrainKey) };
        if (FoundTerrain != TerrainBucket.mNameLookup.end() && FoundTerrain->second < TerrainBucket.mAssets.size()) {
            return TerrainBucket.mAssets[FoundTerrain->second];
        }

        TerrainTiledMeshData TiledMeshData{};
        HeightFieldData HeightField{};
        try {
            TerrainHeightFieldFactory HeightFieldFactory{};
            TerrainTiledMeshBuilder Builder{};
            HeightField = HeightFieldFactory.Build(ResolvedDesc);
            TiledMeshData = Builder.Build(HeightField, ResolvedDesc);
        }
        catch (const std::exception& Exception) {
            ErrorHandler::report("TerrainRenderResource", Exception.what(), ErrorHandler::Level::Warning);
            return nullptr;
        }

        std::shared_ptr<Model> NewModel{ std::make_shared<Model>() };
        if (mDevice != nullptr && mCopyQueue != nullptr && mAllocator != nullptr) {
            const bool IsInitialized{ NewModel->InitializeFromModelResult(TiledMeshData.mModelData, mAllocator, mCopyQueue) };
            if (IsInitialized == false) {
                return nullptr;
            }
        }

        std::shared_ptr<TerrainRenderResource> NewResource{ std::make_shared<TerrainRenderResource>() };
        std::shared_ptr<const HeightFieldData> HeightFieldPointer{ std::make_shared<const HeightFieldData>(std::move(HeightField)) };
        NewResource->Initialize(NewModel, std::move(TiledMeshData.mTileMetadata), TiledMeshData.mTileQuadCount, TiledMeshData.mTileCountX, TiledMeshData.mTileCountZ, TiledMeshData.mLodCount, std::move(TiledMeshData.mLodDistances), TiledMeshData.mLocalBoundingBox, ResolvedDesc);
        if (mDevice != nullptr && mCopyQueue != nullptr && mAllocator != nullptr && mSrvHeap != nullptr) {
            const bool IsHeightFieldInitialized{ NewResource->InitializeHeightField(HeightFieldPointer, ResolvedDesc, mDevice, mCopyQueue, mAllocator, mSrvHeap) };
            if (IsHeightFieldInitialized == false) {
                ErrorHandler::report("TerrainRenderResource", "Failed to initialize terrain height field GPU resource.", ErrorHandler::Level::Warning);
                return nullptr;
            }
        }

        const std::uint32_t NewIndex{ static_cast<std::uint32_t>(TerrainBucket.mAssets.size()) };
        TerrainBucket.mAssets.push_back(NewResource);
        TerrainBucket.mNameLookup.insert_or_assign(TerrainKey, NewIndex);
        return NewResource;
    }

    bool AssetRegistry::UpdateTerrainStreaming(TerrainRenderResource& Resource, const TerrainManager& TerrainManagerInstance, const SimpleMath::Vector3& FocusPosition, std::uint32_t FrameIndex) {
        if (mDevice == nullptr || mCopyQueue == nullptr || mAllocator == nullptr || mSrvHeap == nullptr) {
            return false;
        }

        return Resource.UpdateStreaming(TerrainManagerInstance, FocusPosition, FrameIndex, mDevice, mCopyQueue, mAllocator, mSrvHeap);
    }

    std::shared_ptr<asset::Animation> AssetRegistry::GetAnimation(const std::string& AnimationBinaryPath) {
        IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        AssetRegistryStorage& Storage{ BackEnd->GetStorage() };
        auto& AnimationBucket{ Storage.GetBucket<AnimationBucketTag>() };

        const auto FoundAnimation{ AnimationBucket.mNameLookup.find(AnimationBinaryPath) };
        if (FoundAnimation != AnimationBucket.mNameLookup.end() && FoundAnimation->second < AnimationBucket.mAssets.size()) {
            return AnimationBucket.mAssets[FoundAnimation->second];
        }

        asset::Animation AnimationData{};
        if (ReadAnimationData(AnimationBinaryPath, AnimationData) == false) {
            return nullptr;
        }

        std::shared_ptr<asset::Animation> NewAnimation{ std::make_shared<asset::Animation>(std::move(AnimationData)) };
        const std::uint32_t NewIndex{ static_cast<std::uint32_t>(AnimationBucket.mAssets.size()) };
        AnimationBucket.mAssets.push_back(NewAnimation);
        AnimationBucket.mNameLookup.insert_or_assign(AnimationBinaryPath, NewIndex);
        return NewAnimation;
    }

    std::shared_ptr<AnimationGraphAsset> AssetRegistry::GetAnimationGraph(const std::string& AnimationGraphPath) {
        IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        AssetRegistryStorage& Storage{ BackEnd->GetStorage() };
        auto& AnimationGraphBucket{ Storage.GetBucket<AnimationGraphBucketTag>() };

        const auto FoundGraph{ AnimationGraphBucket.mNameLookup.find(AnimationGraphPath) };
        if (FoundGraph != AnimationGraphBucket.mNameLookup.end() && FoundGraph->second < AnimationGraphBucket.mAssets.size()) {
            return AnimationGraphBucket.mAssets[FoundGraph->second];
        }

        AnimationGraphAsset AnimationGraphData{};
        if (ReadAnimationGraphData(AnimationGraphPath, AnimationGraphData) == false) {
            return nullptr;
        }

        std::shared_ptr<AnimationGraphAsset> NewGraph{ std::make_shared<AnimationGraphAsset>(std::move(AnimationGraphData)) };
        const std::uint32_t NewIndex{ static_cast<std::uint32_t>(AnimationGraphBucket.mAssets.size()) };
        AnimationGraphBucket.mAssets.push_back(NewGraph);
        AnimationGraphBucket.mNameLookup.insert_or_assign(AnimationGraphPath, NewIndex);
        return NewGraph;
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

    std::string AssetRegistry::FindTerrainRenderResourceSelectorByPointer(const TerrainRenderResource* TerrainRenderResourcePointer) const {
        if (TerrainRenderResourcePointer == nullptr) {
            return std::string{};
        }

        const IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        const AssetRegistryStorage& Storage{ BackEnd->GetStorage() };
        const auto& TerrainBucket{ Storage.GetBucket<TerrainRenderResourceBucketTag>() };
        for (const auto& Pair : TerrainBucket.mNameLookup) {
            const std::uint32_t TerrainIndex{ Pair.second };
            if (TerrainIndex >= TerrainBucket.mAssets.size()) {
                continue;
            }

            const std::shared_ptr<TerrainRenderResource>& RegisteredResource{ TerrainBucket.mAssets[TerrainIndex] };
            if (RegisteredResource.get() == TerrainRenderResourcePointer) {
                return Pair.first;
            }
        }

        return std::string{};
    }

    std::string AssetRegistry::FindAnimationSelectorByPointer(const asset::Animation* AnimationPointer) const {
        if (AnimationPointer == nullptr) {
            return std::string{};
        }

        const IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        const AssetRegistryStorage& Storage{ BackEnd->GetStorage() };
        const auto& AnimationBucket{ Storage.GetBucket<AnimationBucketTag>() };
        for (const auto& Pair : AnimationBucket.mNameLookup) {
            const std::uint32_t AnimationIndex{ Pair.second };
            if (AnimationIndex >= AnimationBucket.mAssets.size()) {
                continue;
            }

            const std::shared_ptr<asset::Animation>& RegisteredAnimation{ AnimationBucket.mAssets[AnimationIndex] };
            if (RegisteredAnimation.get() == AnimationPointer) {
                return Pair.first;
            }
        }

        return std::string{};
    }

    std::string AssetRegistry::FindAnimationGraphSelectorByPointer(const AnimationGraphAsset* AnimationGraphPointer) const {
        if (AnimationGraphPointer == nullptr) {
            return std::string{};
        }

        const IAssetRegistryBackEnd* BackEnd{ mBackEnd.get() };
        const AssetRegistryStorage& Storage{ BackEnd->GetStorage() };
        const auto& AnimationGraphBucket{ Storage.GetBucket<AnimationGraphBucketTag>() };
        for (const auto& Pair : AnimationGraphBucket.mNameLookup) {
            const std::uint32_t GraphIndex{ Pair.second };
            if (GraphIndex >= AnimationGraphBucket.mAssets.size()) {
                continue;
            }

            const std::shared_ptr<AnimationGraphAsset>& RegisteredGraph{ AnimationGraphBucket.mAssets[GraphIndex] };
            if (RegisteredGraph.get() == AnimationGraphPointer) {
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

    void AppendUsedTextureTableIndices(const std::vector<std::vector<std::uint32_t>>& MaterialToTextureTableIndices, const std::vector<RFD::DrawRecord>& DrawRecords, std::unordered_set<std::uint32_t>& InOutUsedTextureTableIndices) {
        for (const RFD::DrawRecord& DrawRecordData : DrawRecords) {
            if (DrawRecordData.materialIndex >= MaterialToTextureTableIndices.size()) {
                continue;
            }

            const std::vector<std::uint32_t>& MaterialTextureIndices{ MaterialToTextureTableIndices[DrawRecordData.materialIndex] };
            for (const std::uint32_t TextureIndex : MaterialTextureIndices) {
                InOutUsedTextureTableIndices.insert(TextureIndex);
            }
        }
    }

    void AppendUsedEnvironmentTextureTableIndices(const std::vector<std::vector<std::uint32_t>>& MaterialToTextureTableIndices, const std::vector<RFD::EnvironmentDrawRecord>& DrawRecords, std::unordered_set<std::uint32_t>& InOutUsedTextureTableIndices) {
        for (const RFD::EnvironmentDrawRecord& DrawRecordData : DrawRecords) {
            if (DrawRecordData.mMaterialIndex >= MaterialToTextureTableIndices.size()) {
                continue;
            }

            const std::vector<std::uint32_t>& MaterialTextureIndices{ MaterialToTextureTableIndices[DrawRecordData.mMaterialIndex] };
            for (const std::uint32_t TextureIndex : MaterialTextureIndices) {
                InOutUsedTextureTableIndices.insert(TextureIndex);
            }
        }
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
        AppendUsedTextureTableIndices(MaterialToTextureTableIndices, RenderData.drawRecords, UsedTextureTableIndices);
        AppendUsedEnvironmentTextureTableIndices(MaterialToTextureTableIndices, RenderData.mEnvironmentDrawRecords, UsedTextureTableIndices);
        for (const RFD::ShadowRenderContext& ShadowRenderContext : RenderData.ShadowRenderContexts) {
            AppendUsedTextureTableIndices(MaterialToTextureTableIndices, ShadowRenderContext.DrawRecords, UsedTextureTableIndices);
            AppendUsedEnvironmentTextureTableIndices(MaterialToTextureTableIndices, ShadowRenderContext.mEnvironmentDrawRecords, UsedTextureTableIndices);
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

    bool AssetRegistry::ReadAnimationData(const std::string& AnimationBinaryPath, asset::Animation& OutAnimationData) const {
        asset::AnimationBinaryReader Reader{};
        return Reader.ReadFromFile(AnimationBinaryPath, OutAnimationData);
    }


    bool AssetRegistry::ReadAnimationGraphData(const std::string& AnimationGraphPath, AnimationGraphAsset& OutAnimationGraphData) const {
        std::ifstream InputStream{ AnimationGraphPath, std::ios::in | std::ios::binary };
        if (InputStream.is_open() == false) {
            return false;
        }

        std::stringstream Buffer{};
        Buffer << InputStream.rdbuf();
        const std::string YamlText{ Buffer.str() };

        c4::yml::Tree Tree{};
        c4::yml::parse_in_arena(c4::to_csubstr(YamlText), &Tree);
        const c4::yml::ConstNodeRef RootNode{ Tree.rootref() };

        std::string GraphName{};
        if (RootNode.has_child("GraphName")) {
            RootNode["GraphName"] >> GraphName;
        }
        OutAnimationGraphData.SetGraphName(GraphName);

        std::unordered_map<std::string, std::uint32_t> ParameterNameToIndex{};
        std::vector<RuntimeParameterDefinition> ParameterDefinitions{};
        if (RootNode.has_child("ParameterDefinitions")) {
            for (const c4::yml::ConstNodeRef ParameterNode : RootNode["ParameterDefinitions"].children()) {
                RuntimeParameterDefinition NewDefinition{};
                std::string ParameterTypeText{};
                if (ParameterNode.has_child("ParameterName")) {
                    ParameterNode["ParameterName"] >> NewDefinition.ParameterName;
                }
                if (ParameterNode.has_child("ParameterType")) {
                    ParameterNode["ParameterType"] >> ParameterTypeText;
                }

                if (ParameterTypeText == "Int") {
                    NewDefinition.ParameterTypeValue = RuntimeParameterDefinition::ParameterType::Int;
                    std::int32_t DefaultIntValue{};
                    if (ParameterNode.has_child("DefaultValue")) {
                        ParameterNode["DefaultValue"] >> DefaultIntValue;
                    }
                    NewDefinition.DefaultValue = DefaultIntValue;
                }
                else if (ParameterTypeText == "Float") {
                    NewDefinition.ParameterTypeValue = RuntimeParameterDefinition::ParameterType::Float;
                    float DefaultFloatValue{};
                    if (ParameterNode.has_child("DefaultValue")) {
                        ParameterNode["DefaultValue"] >> DefaultFloatValue;
                    }
                    NewDefinition.DefaultValue = DefaultFloatValue;
                }
                else if (ParameterTypeText == "Trigger") {
                    NewDefinition.ParameterTypeValue = RuntimeParameterDefinition::ParameterType::Trigger;
                    bool DefaultTriggerValue{};
                    if (ParameterNode.has_child("DefaultValue")) {
                        ParameterNode["DefaultValue"] >> DefaultTriggerValue;
                    }
                    NewDefinition.DefaultValue = DefaultTriggerValue;
                }
                else {
                    NewDefinition.ParameterTypeValue = RuntimeParameterDefinition::ParameterType::Bool;
                    bool DefaultBoolValue{};
                    if (ParameterNode.has_child("DefaultValue")) {
                        ParameterNode["DefaultValue"] >> DefaultBoolValue;
                    }
                    NewDefinition.DefaultValue = DefaultBoolValue;
                }

                const std::uint32_t ParameterIndex{ static_cast<std::uint32_t>(ParameterDefinitions.size()) };
                ParameterNameToIndex.insert_or_assign(NewDefinition.ParameterName, ParameterIndex);
                ParameterDefinitions.push_back(std::move(NewDefinition));
            }
        }
        OutAnimationGraphData.SetParameterDefinitions(std::move(ParameterDefinitions));

        std::unordered_map<std::string, std::uint32_t> NodeNameToIndex{};
        std::vector<AnimationGraphAsset::AnimationGraphNodeAsset> Nodes{};
        if (RootNode.has_child("Nodes")) {
            for (const c4::yml::ConstNodeRef NodeNode : RootNode["Nodes"].children()) {
                AnimationGraphAsset::AnimationGraphNodeAsset NewNode{};
                if (NodeNode.has_child("NodeName")) {
                    NodeNode["NodeName"] >> NewNode.NodeName;
                }
                if (NodeNode.has_child("AnimationFile")) {
                    NodeNode["AnimationFile"] >> NewNode.AnimationFile;
                }
                if (NodeNode.has_child("ClipIndex")) {
                    NodeNode["ClipIndex"] >> NewNode.ClipIndex;
                }
                if (NodeNode.has_child("IsLoop")) {
                    NodeNode["IsLoop"] >> NewNode.IsLoop;
                }
                if (NodeNode.has_child("PlaySpeed")) {
                    NodeNode["PlaySpeed"] >> NewNode.PlaySpeed;
                }
                if (NodeNode.has_child("IsDefault")) {
                    NodeNode["IsDefault"] >> NewNode.IsDefault;
                }

                const std::uint32_t NodeIndex{ static_cast<std::uint32_t>(Nodes.size()) };
                NodeNameToIndex.insert_or_assign(NewNode.NodeName, NodeIndex);
                Nodes.push_back(std::move(NewNode));
            }
        }
        OutAnimationGraphData.SetNodes(std::move(Nodes));

        std::vector<AnimationGraphAsset::AnimationGraphTransitionAsset> Transitions{};
        std::uint32_t FileOrder{};
        if (RootNode.has_child("Transitions")) {
            for (const c4::yml::ConstNodeRef TransitionNode : RootNode["Transitions"].children()) {
                AnimationGraphAsset::AnimationGraphTransitionAsset NewTransition{};
                std::string FromNodeName{};
                std::string ToNodeName{};
                if (TransitionNode.has_child("FromNode")) {
                    TransitionNode["FromNode"] >> FromNodeName;
                }
                if (TransitionNode.has_child("ToNode")) {
                    TransitionNode["ToNode"] >> ToNodeName;
                }

                const std::unordered_map<std::string, std::uint32_t>::const_iterator FoundFromNode{ NodeNameToIndex.find(FromNodeName) };
                const std::unordered_map<std::string, std::uint32_t>::const_iterator FoundToNode{ NodeNameToIndex.find(ToNodeName) };
                if (FoundFromNode == NodeNameToIndex.end() || FoundToNode == NodeNameToIndex.end()) {
                    return false;
                }

                NewTransition.FromNodeIndex = FoundFromNode->second;
                NewTransition.ToNodeIndex = FoundToNode->second;
                NewTransition.FileOrder = FileOrder;
                FileOrder += 1;

                if (TransitionNode.has_child("HasExitTime")) {
                    TransitionNode["HasExitTime"] >> NewTransition.HasExitTime;
                }
                if (TransitionNode.has_child("ExitTimeNormalized")) {
                    TransitionNode["ExitTimeNormalized"] >> NewTransition.ExitTimeNormalized;
                }
                if (TransitionNode.has_child("CanInterrupt")) {
                    TransitionNode["CanInterrupt"] >> NewTransition.CanInterrupt;
                }
                if (TransitionNode.has_child("Priority")) {
                    TransitionNode["Priority"] >> NewTransition.Priority;
                }
                if (TransitionNode.has_child("BlendDuration")) {
                    TransitionNode["BlendDuration"] >> NewTransition.BlendDuration;
                }

                if (TransitionNode.has_child("Conditions")) {
                    for (const c4::yml::ConstNodeRef ConditionNode : TransitionNode["Conditions"].children()) {
                        AnimationGraphAsset::AnimationGraphConditionAsset NewCondition{};
                        std::string ParameterName{};
                        std::string OperatorText{};

                        if (ConditionNode.has_child("Parameter")) {
                            ConditionNode["Parameter"] >> ParameterName;
                        }
                        if (ConditionNode.has_child("Operator")) {
                            ConditionNode["Operator"] >> OperatorText;
                        }

                        const std::unordered_map<std::string, std::uint32_t>::const_iterator FoundParameter{ ParameterNameToIndex.find(ParameterName) };
                        if (FoundParameter == ParameterNameToIndex.end()) {
                            return false;
                        }

                        NewCondition.ParameterIndex = FoundParameter->second;
                        if (OperatorText == "NotEquals") {
                            NewCondition.Operator = AnimationGraphAsset::ConditionOperator::NotEquals;
                        }
                        else if (OperatorText == "Greater") {
                            NewCondition.Operator = AnimationGraphAsset::ConditionOperator::Greater;
                        }
                        else if (OperatorText == "GreaterOrEqual") {
                            NewCondition.Operator = AnimationGraphAsset::ConditionOperator::GreaterOrEqual;
                        }
                        else if (OperatorText == "Less") {
                            NewCondition.Operator = AnimationGraphAsset::ConditionOperator::Less;
                        }
                        else if (OperatorText == "LessOrEqual") {
                            NewCondition.Operator = AnimationGraphAsset::ConditionOperator::LessOrEqual;
                        }
                        else {
                            NewCondition.Operator = AnimationGraphAsset::ConditionOperator::Equals;
                        }

                        if (ConditionNode.has_child("BoolValue")) {
                            ConditionNode["BoolValue"] >> NewCondition.BoolValue;
                        }
                        if (ConditionNode.has_child("IntValue")) {
                            ConditionNode["IntValue"] >> NewCondition.IntValue;
                        }
                        if (ConditionNode.has_child("FloatValue")) {
                            ConditionNode["FloatValue"] >> NewCondition.FloatValue;
                        }

                        NewTransition.Conditions.push_back(std::move(NewCondition));
                    }
                }

                Transitions.push_back(std::move(NewTransition));
            }
        }

        OutAnimationGraphData.SetTransitions(std::move(Transitions));

        std::string ErrorText{};
        return OutAnimationGraphData.Validate(ErrorText);
    }

    bool AssetRegistry::ReadMaterialGroups(const std::string& MaterialJsonPath, std::vector<asset::MaterialGroup>& OutMaterialGroups) const {
        asset::MaterialGroupJsonSerializer Serializer{};
        return Serializer.ReadFromFile(MaterialJsonPath, OutMaterialGroups);
    }
}
