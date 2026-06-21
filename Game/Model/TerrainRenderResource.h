#pragma once

#include <array>
#include <cstdint>
#include <future>
#include <memory>
#include <vector>

#include "Core/Common.h"
#include "Core/DX/DesciptorHeap.h"
#include "Game/Model/Model.h"
#include "Game/Model/TerrainMeshTypes.h"
#include "Game/Terrain/TerrainManager.h"
#include "Utility/CompileTimeConstants.h"
#include "Utility/DirectXInclude.h"

namespace Game {
    struct TerrainFrameBufferResource final {
    public:
        std::unique_ptr<Interface::IAllocationHandle> mAllocation{};
        Core::DX::DescriptorHandle mSrvHandle{};
        std::uint32_t mSrvDescriptorIndex{ 0xffffffffu };
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
        void Initialize(std::shared_ptr<Model> ModelValue, std::vector<TerrainTileMetadata> TileMetadataValue, std::uint32_t TileQuadCountValue, std::uint32_t TileCountXValue, std::uint32_t TileCountZValue, std::uint32_t LodCountValue, std::vector<float> LodDistancesValue, const DirectX::BoundingOrientedBox& LocalBoundingBoxValue, const TerrainBuildDesc& BuildDescValue);
        bool InitializeHeightField(const std::shared_ptr<const HeightFieldData>& Field, const TerrainBuildDesc& Desc, ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Interface::IDescriptorHeap* SrvHeap);
        bool UpdateStreaming(const TerrainManager& TerrainManagerInstance, const SimpleMath::Vector3& FocusPosition, std::uint32_t FrameIndex, ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Interface::IDescriptorHeap* SrvHeap);
        const std::shared_ptr<Model>& GetModel() const;
        const std::vector<TerrainTileMetadata>& GetTileMetadata() const;
        const TerrainBuildDesc& GetBuildDesc() const;
        const HeightFieldData& GetHeightFieldData() const;
        const std::shared_ptr<const HeightFieldData>& GetHeightFieldDataPointer() const;
        const SplatMapData& GetSplatMapData() const;
        const std::shared_ptr<const SplatMapData>& GetSplatMapDataPointer() const;
        std::uint32_t GetTileQuadCount() const;
        std::uint32_t GetTileCountX() const;
        std::uint32_t GetTileCountZ() const;
        std::uint32_t GetLodCount() const;
        const std::vector<float>& GetLodDistances() const;
        float GetLodExponent() const;
        const DirectX::BoundingOrientedBox& GetLocalBoundingBox() const;
        std::uint32_t GetHeightFieldSrvDescriptorIndex(std::uint32_t FrameIndex) const;
        std::uint32_t GetSplatMapSrvDescriptorIndex(std::uint32_t FrameIndex) const;
        std::uint32_t GetHeightFieldWidth() const;
        std::uint32_t GetHeightFieldHeight() const;
        std::uint32_t GetSplatMapWidth() const;
        std::uint32_t GetSplatMapHeight() const;
        float GetMaxHeight() const;
        float GetCellSizeX() const;
        float GetCellSizeZ() const;
        float GetOriginOffsetX() const;
        float GetOriginOffsetZ() const;
        bool IsHeightFieldFlipV() const;
        bool IsStreamingEnabled() const;
        std::int32_t GetStreamOriginGridX() const;
        std::int32_t GetStreamOriginGridZ() const;
        float GetStreamWorldOriginX() const;
        float GetStreamWorldOriginZ() const;
        RenderContract::Future GetFrameUploadFuture(std::uint32_t FrameIndex) const;

    private:
        bool EnsureHeightFieldFrameResource(const HeightFieldData& Field, std::uint32_t FrameIndex, std::size_t HeightFieldSizeInBytes, ID3D12Device* Device, Interface::IGraphicsAllocator* Allocator, Interface::IDescriptorHeap* SrvHeap);
        bool EnsureSplatMapFrameResource(const SplatMapData& SplatMap, std::uint32_t FrameIndex, std::size_t SplatMapSizeInBytes, ID3D12Device* Device, Interface::IGraphicsAllocator* Allocator, Interface::IDescriptorHeap* SrvHeap);
        bool UploadTerrainFrameData(const HeightFieldData& Field, const SplatMapData& SplatMap, std::uint32_t FrameIndex, ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Interface::IDescriptorHeap* SrvHeap);
        bool EnsureTerrainFrameData(std::uint32_t FrameIndex, ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Interface::IDescriptorHeap* SrvHeap);
        bool IsTerrainFrameCopyInFlight(std::uint32_t FrameIndex) const;
        bool IsAnyTerrainFrameCopyInFlight() const;
        bool TryCommitStreamingBuild(TerrainStreamingBuildResult&& Result, std::uint32_t FrameIndex, ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Interface::IDescriptorHeap* SrvHeap);
        void MarkTerrainFrameResourcesDirty();
        void StartStreamingBuild(const TerrainManager& TerrainManagerInstance, const TerrainBuildDesc& StreamingDesc, std::int32_t TargetOriginGridX, std::int32_t TargetOriginGridZ);

    private:
        std::shared_ptr<Model> mModel{};
        std::vector<TerrainTileMetadata> mTileMetadata{};
        TerrainBuildDesc mBuildDesc{};
        std::shared_ptr<const HeightFieldData> mHeightFieldData{};
        std::shared_ptr<const SplatMapData> mSplatMapData{};
        std::uint32_t mTileQuadCount{ 0 };
        std::uint32_t mTileCountX{ 0 };
        std::uint32_t mTileCountZ{ 0 };
        std::uint32_t mLodCount{ 1 };
        std::vector<float> mLodDistances{};
        DirectX::BoundingOrientedBox mLocalBoundingBox{};
        std::array<TerrainFrameBufferResource, Constants::FrameCount<std::size_t>> mHeightFieldFrameResources{};
        std::array<TerrainFrameBufferResource, Constants::FrameCount<std::size_t>> mSplatMapFrameResources{};
        std::array<RenderContract::Future, Constants::FrameCount<std::size_t>> mTerrainFrameCopyFutures{};
        std::array<bool, Constants::FrameCount<std::size_t>> mTerrainFrameDirtyFlags{};
        std::uint32_t mHeightFieldWidth{ 0 };
        std::uint32_t mHeightFieldHeight{ 0 };
        std::uint32_t mSplatMapWidth{ 0 };
        std::uint32_t mSplatMapHeight{ 0 };
        float mMaxHeight{ 1.0f };
        float mCellSizeX{ 1.0f };
        float mCellSizeZ{ 1.0f };
        float mOriginOffsetX{ 0.0f };
        float mOriginOffsetZ{ 0.0f };
        bool mHeightFieldFlipV{ false };
        std::int32_t mStreamOriginGridX{ 0 };
        std::int32_t mStreamOriginGridZ{ 0 };
        float mStreamWorldOriginX{ 0.0f };
        float mStreamWorldOriginZ{ 0.0f };
        std::future<TerrainStreamingBuildResult> mStreamingBuildFuture{};
        std::int32_t mPendingStreamingOriginGridX{ 0 };
        std::int32_t mPendingStreamingOriginGridZ{ 0 };
        bool mHasPendingStreamingBuild{ false };
        bool mHasStreamOrigin{ false };
    };
}
