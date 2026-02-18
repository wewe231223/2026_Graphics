#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include "Asset/AssetBundle.h"
#include "Core/DX/CopyQueue.h"
#include "Core/DX/GraphicsAllocator.h"
#include "Model.h"

namespace Game {
    struct RegisteredMaterial final {
        std::string Name{};
        asset::Material Data{};
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
        void Initialize(ID3D12Device* Device, Core::DX::CopyQueue* CopyQueue, Core::DX::GraphicsAllocator* Allocator);

        std::shared_ptr<Model> GetModel(const std::string& ModelKey, const asset::AssetBundle& Bundle);

        std::uint32_t AddMaterial(const asset::Material& MaterialData);
        std::uint32_t FindMaterialIndexByName(const std::string& MaterialName) const;
        const std::vector<RegisteredMaterial>& GetMaterials() const;

    private:
        ID3D12Device* mDevice{ nullptr };
        Core::DX::CopyQueue* mCopyQueue{ nullptr };
        Core::DX::GraphicsAllocator* mAllocator{ nullptr };
        std::unordered_map<std::string, std::shared_ptr<Model>> mModelCache{};
        std::vector<RegisteredMaterial> mMaterials{};
        std::unordered_map<std::string, std::uint32_t> mMaterialNameLookup{};
    };
}
