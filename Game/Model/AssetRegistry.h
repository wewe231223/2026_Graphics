#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>
#include "Core/Common.h"
#include "Asset/ModelResult.h"
#include "Model.h"
#include "Game/Base/Common.h"
#include "Game/Base/Pipeline.h"

namespace Game {
    struct RegisteredMaterial final {
        std::string Name{};
        asset::Material Data{};
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

        std::shared_ptr<Model> GetModel(const std::string& ModelBinaryPath);
        bool LoadMaterialGroups(const std::string& MaterialJsonPath);

        std::uint32_t AddMaterial(const asset::Material& MaterialData);
        std::uint32_t AddMaterialGroup(const asset::MaterialGroup& MaterialGroupData);
        std::uint32_t FindMaterialIndexByName(const std::string& MaterialName) const;
        const std::vector<RegisteredMaterial>& GetMaterials() const;
        const std::vector<RegisteredMaterialGroup>& GetMaterialGroups() const;
        std::uint32_t FindMaterialGroupIndexBySourcePath(const std::string& MaterialSourcePath) const;

    private:
        Interface::IPipeline* ResolvePipelineByName(const std::string& PipelineName);
        std::uint32_t AddMaterialGroupWithSource(const asset::MaterialGroup& MaterialGroupData, const std::string& SourcePath);
        bool ReadModelData(const std::string& ModelBinaryPath, asset::ModelResult& OutModelData) const;
        bool ReadMaterialGroups(const std::string& MaterialJsonPath, std::vector<asset::MaterialGroup>& OutMaterialGroups) const;

    private:
        ID3D12Device* mDevice{ nullptr };
        Interface::ICopyQueue* mCopyQueue{ nullptr };
        Interface::IGraphicsAllocator* mAllocator{ nullptr };
        std::unordered_map<std::string, std::shared_ptr<Model>> mModelCache{};
        std::unordered_set<std::string> mLoadedMaterialJsonPaths{};

        std::vector<RegisteredMaterial> mMaterials{};
        std::unordered_map<std::string, std::uint32_t> mMaterialNameLookup{};

        std::vector<RegisteredMaterialGroup> mMaterialGroups{};
        std::unordered_map<std::string, std::uint32_t> mMaterialGroupNameLookup{};
        std::vector<std::string> mMaterialGroupSourcePaths{};

        std::vector<std::unique_ptr<Base::Pipeline>> mPipelines{};
        std::unordered_map<std::string, Interface::IPipeline*> mPipelineLookup{};
    };
}
