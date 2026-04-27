#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "Core/Common.h"
#include "Core/DX/DesciptorHeap.h"
#include "Game/Model/Model.h"
#include "Game/Model/TerrainMeshTypes.h"
#include "Utility/DirectXInclude.h"

namespace Game {
    struct TerrainTileMetadata final {
    public:
        std::uint32_t mTileIndexX{ 0 };
        std::uint32_t mTileIndexZ{ 0 };
        std::uint32_t mStartX{ 0 };
        std::uint32_t mStartZ{ 0 };
        std::uint32_t mQuadCountX{ 0 };
        std::uint32_t mQuadCountZ{ 0 };
        DirectX::BoundingOrientedBox mLocalBoundingBox{};
        SimpleMath::Vector3 mCenter{};
        std::uint32_t mSubMeshIndex{ 0 };
        std::vector<std::uint32_t> mSubMeshIndexByLod{};
    };

    class TerrainRenderResource final {
    public:
        TerrainRenderResource();
        ~TerrainRenderResource();
        TerrainRenderResource(const TerrainRenderResource& Other) = delete;
        TerrainRenderResource& operator=(const TerrainRenderResource& Other) = delete;
        TerrainRenderResource(TerrainRenderResource&& Other) noexcept;
        TerrainRenderResource& operator=(TerrainRenderResource&& Other) noexcept;

    public:
        void Initialize(std::shared_ptr<Model> ModelValue, std::vector<TerrainTileMetadata> TileMetadataValue, std::uint32_t TileQuadCountValue, std::uint32_t TileCountXValue, std::uint32_t TileCountZValue, std::uint32_t LodCountValue, std::vector<float> LodDistancesValue, const DirectX::BoundingOrientedBox& LocalBoundingBoxValue);
        bool InitializeHeightField(const HeightFieldData& Field, const TerrainBuildDesc& Desc, ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Interface::IDescriptorHeap* SrvHeap);
        const std::shared_ptr<Model>& GetModel() const;
        const std::vector<TerrainTileMetadata>& GetTileMetadata() const;
        std::uint32_t GetTileQuadCount() const;
        std::uint32_t GetTileCountX() const;
        std::uint32_t GetTileCountZ() const;
        std::uint32_t GetLodCount() const;
        const std::vector<float>& GetLodDistances() const;
        const DirectX::BoundingOrientedBox& GetLocalBoundingBox() const;
        std::uint32_t GetHeightFieldSrvDescriptorIndex() const;
        std::uint32_t GetHeightFieldWidth() const;
        std::uint32_t GetHeightFieldHeight() const;
        float GetMaxHeight() const;
        float GetCellSizeX() const;
        float GetCellSizeZ() const;
        float GetOriginOffsetX() const;
        float GetOriginOffsetZ() const;
        bool IsHeightFieldFlipV() const;

    private:
        std::shared_ptr<Model> mModel{};
        std::vector<TerrainTileMetadata> mTileMetadata{};
        std::uint32_t mTileQuadCount{ 0 };
        std::uint32_t mTileCountX{ 0 };
        std::uint32_t mTileCountZ{ 0 };
        std::uint32_t mLodCount{ 1 };
        std::vector<float> mLodDistances{};
        DirectX::BoundingOrientedBox mLocalBoundingBox{};
        std::unique_ptr<Interface::IAllocationHandle> mHeightFieldAllocation{};
        Interface::CopyFuture mHeightFieldCopyFuture{};
        Core::DX::DescriptorHandle mHeightFieldSrvHandle{};
        std::uint32_t mHeightFieldSrvDescriptorIndex{ 0xffffffffu };
        std::uint32_t mHeightFieldWidth{ 0 };
        std::uint32_t mHeightFieldHeight{ 0 };
        float mMaxHeight{ 1.0f };
        float mCellSizeX{ 1.0f };
        float mCellSizeZ{ 1.0f };
        float mOriginOffsetX{ 0.0f };
        float mOriginOffsetZ{ 0.0f };
        bool mHeightFieldFlipV{ false };
    };
}
