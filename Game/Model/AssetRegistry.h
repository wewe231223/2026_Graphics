#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <filesystem>
#include <functional>
#include <vector>
#include <utility>
#include "Core/Common.h"
#include "Core/DX/Texture.h"
#include "Asset/ModelResult.h"
#include "Model.h"
#include "Game/Base/Common.h"
#include "Game/Base/Pipeline.h"
#include "Game/Base/RenderFrameData.h"
#include "Game/Model/AssetRegistryBackEnd.h"

namespace Game {
    class AssetRegistry final {
    public:
        using TextureResidencyDecider = std::function<bool(std::uint32_t TextureTableIndex, bool IsUsedByDrawRecords, bool IsLoaded)>;

    public:
        AssetRegistry();
        ~AssetRegistry();
        AssetRegistry(const AssetRegistry& Other) = delete;
        AssetRegistry& operator=(const AssetRegistry& Other) = delete;
        AssetRegistry(AssetRegistry&& Other) noexcept;
        AssetRegistry& operator=(AssetRegistry&& Other) noexcept;

    public:
        void Initialize(ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator);
        void SetSrvHeap(Interface::IDescriptorHeap* SrvHeap);

        void SetBackEnd(const std::shared_ptr<IAssetRegistryBackEnd>& NewBackEnd);
        std::shared_ptr<IAssetRegistryBackEnd> GetBackEnd() const;

        std::shared_ptr<Model> GetModel(const std::string& ModelBinaryPath);
        bool LoadMaterialGroups(const std::string& MaterialJsonPath);

        std::uint32_t AddMaterial(const asset::Material& MaterialData, const std::string& MaterialSourcePath);
        std::uint32_t AddMaterialGroup(const asset::MaterialGroup& MaterialGroupData);
        std::uint32_t FindMaterialIndexByName(const std::string& MaterialName) const;
        const std::vector<RegisteredMaterial>& GetMaterials() const;
        const std::vector<RegisteredMaterialGroup>& GetMaterialGroups() const;
        const std::vector<RFD::MaterialGpu>& GetPackedMaterials() const;
        const std::vector<RFD::MaterialTextureTableItemGpu>& GetMaterialTextureTable() const;
        std::uint32_t FindMaterialGroupIndexBySourcePath(const std::string& MaterialSourcePath) const;
        std::string FindModelSelectorByPointer(const Model* ModelPointer) const;
        std::string FindMaterialGroupSourcePathByIndex(std::uint32_t MaterialGroupIndex) const;
        Interface::IPipeline* GetPipelineByName(const std::string& PipelineName);

        void SetTextureResidencyDecider(TextureResidencyDecider NewDecider);
        void PrepareRenderTextures(const RFD::RenderFrameData& RenderData);

    private:
        std::uint32_t ResolveTextureTableIndex(const std::filesystem::path& TexturePath);
        bool ShouldKeepTextureResident(const AssetRegistryTextureRecord& TextureData, const std::unordered_set<std::uint32_t>& UsedTextureTableIndices) const;
        void UpdateTextureTableItem(AssetRegistryTextureRecord& TextureData);
        std::vector<std::uint32_t> BuildMaterialTextureTableIndices(const asset::Material& MaterialData, const RFD::MaterialGpu& PackedMaterial) const;

        std::filesystem::path BuildTexturePathFromMaterialPath(const std::string& MaterialSourcePath, const std::string& TexturePath) const;
        std::int64_t ToMaterialIntValue(const asset::MaterialMap& MaterialMapData, const std::string& MaterialSourcePath);
        SimpleMath::Vector4 ToMaterialFloatValue(const asset::MaterialMap& MaterialMapData);
        RFD::MaterialGpu BuildPackedMaterial(const asset::Material& MaterialData, const std::string& MaterialSourcePath);
        Interface::IPipeline* ResolvePipelineByName(const std::string& PipelineName);
        std::uint32_t AddMaterialGroupWithSource(const asset::MaterialGroup& MaterialGroupData, const std::string& SourcePath);

        bool ReadModelData(const std::string& ModelBinaryPath, asset::ModelResult& OutModelData) const;
        bool ReadMaterialGroups(const std::string& MaterialJsonPath, std::vector<asset::MaterialGroup>& OutMaterialGroups) const;

    private:
        ID3D12Device* mDevice{ nullptr };
        Interface::ICopyQueue* mCopyQueue{ nullptr };
        Interface::IGraphicsAllocator* mAllocator{ nullptr };
        Interface::IDescriptorHeap* mSrvHeap{ nullptr };
        std::shared_ptr<IAssetRegistryBackEnd> mBackEnd{};
        TextureResidencyDecider mTextureResidencyDecider{};
    };
}
