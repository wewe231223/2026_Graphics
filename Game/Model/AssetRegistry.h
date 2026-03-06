#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <functional>
#include <vector>
#include <utility>
#include "Core/Common.h"
#include "Core/DX/Texture.h"
#include "Core/DX/DesciptorHeap.h"
#include "Asset/ModelResult.h"
#include "Model.h"
#include "Game/Base/Common.h"
#include "Game/Base/Pipeline.h"
#include "Game/Base/RenderFrameData.h"

namespace Game {
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

    class AssetRegistry final {
    public:
        AssetRegistry();
        ~AssetRegistry();
        AssetRegistry(const AssetRegistry& Other) = delete;
        AssetRegistry& operator=(const AssetRegistry& Other) = delete;
        AssetRegistry(AssetRegistry&& Other) noexcept;
        AssetRegistry& operator=(AssetRegistry&& Other) noexcept;

    public:
        void Initialize(ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator);
        void SetSrvHeap(Core::DX::DescriptorHeap* SrvHeap);

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

        using TextureResidencyDecider = std::function<bool(std::uint32_t TextureTableIndex, bool IsUsedByDrawRecords, bool IsLoaded)>;

        void SetTextureResidencyDecider(TextureResidencyDecider NewDecider);
        void PrepareRenderTextures(const RFD::RenderFrameData& RenderData);

    private:
        struct TextureRecord final {
            std::string Key{};
            Core::DX::TexPtr Texture{};
            std::uint32_t TableIndex{ 0 };
            bool KeepResident{ false };
        };

    private:
        std::uint32_t ResolveTextureTableIndex(const std::filesystem::path& TexturePath);
        bool ShouldKeepTextureResident(const TextureRecord& TextureData, const std::unordered_set<std::uint32_t>& UsedTextureTableIndices) const;
        void UpdateTextureTableItem(TextureRecord& TextureData);
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
        Core::DX::DescriptorHeap* mSrvHeap{ nullptr };
        std::unordered_map<std::string, std::shared_ptr<Model>> mModelCache{};
        std::unordered_set<std::string> mLoadedMaterialJsonPaths{};

        std::vector<RegisteredMaterial> mMaterials{};
        std::vector<RFD::MaterialGpu> mPackedMaterials{};
        std::unordered_map<std::string, std::uint32_t> mMaterialNameLookup{};

        std::vector<RFD::MaterialTextureTableItemGpu> mMaterialTextureTable{};
        std::unordered_map<std::string, std::uint32_t> mTextureTableLookup{};
        std::vector<TextureRecord> mTextureRecords{};
        std::vector<std::vector<std::uint32_t>> mMaterialToTextureTableIndices{};
        TextureResidencyDecider mTextureResidencyDecider{};

        std::vector<RegisteredMaterialGroup> mMaterialGroups{};
        std::unordered_map<std::string, std::uint32_t> mMaterialGroupNameLookup{};
        std::vector<std::string> mMaterialGroupSourcePaths{};
        std::unordered_map<std::string, std::uint32_t> mMaterialGroupSourcePathLookup{};

        std::vector<std::unique_ptr<Base::Pipeline>> mPipelines{};
        std::unordered_map<std::string, Interface::IPipeline*> mPipelineLookup{};
    };
}
