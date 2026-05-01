#include "TerrainRenderResource.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

#include "Game/Model/TerrainHeightFieldFactory.h"
#include "Game/Model/TerrainTiledMeshBuilder.h"

namespace {
    std::uint32_t ResolveStreamingGridStep(const Game::TerrainBuildDesc& Desc) {
        if (Desc.mStreamingGridStep > 0u) {
            return Desc.mStreamingGridStep;
        }

        return (std::max)(Desc.TileQuadCount, 1u);
    }

    std::int32_t FloorToStep(std::int32_t Value, std::uint32_t Step) {
        const std::int32_t StepValue{ static_cast<std::int32_t>((std::max)(Step, 1u)) };
        if (Value >= 0) {
            return (Value / StepValue) * StepValue;
        }

        return -(((-Value + StepValue - 1) / StepValue) * StepValue);
    }

    std::int32_t CalculateStreamingOriginGrid(float FocusPosition, float CellSize, std::uint32_t HeightFieldVertexCount, std::uint32_t Step) {
        const float SafeCellSize{ CellSize > 0.0f ? CellSize : 1.0f };
        const std::uint32_t QuadCount{ HeightFieldVertexCount > 1u ? HeightFieldVertexCount - 1u : 1u };
        const std::int32_t FocusGrid{ static_cast<std::int32_t>(std::floor(FocusPosition / SafeCellSize)) };
        const std::int32_t HalfGrid{ static_cast<std::int32_t>(QuadCount / 2u) };
        return FloorToStep(FocusGrid - HalfGrid, Step);
    }

    float CalculateStreamingWorldOrigin(std::int32_t OriginGrid, std::uint32_t HeightFieldVertexCount, float CellSize, bool CenterOrigin) {
        if (CenterOrigin == true) {
            const float HalfGrid{ HeightFieldVertexCount > 1u ? static_cast<float>(HeightFieldVertexCount - 1u) * 0.5f : 0.0f };
            return (static_cast<float>(OriginGrid) + HalfGrid) * CellSize;
        }

        return static_cast<float>(OriginGrid) * CellSize;
    }
}

namespace Game {
    TerrainRenderResource::TerrainRenderResource()
        : mModel{},
        mTileMetadata{},
        mBuildDesc{},
        mHeightFieldData{},
        mTileQuadCount{ 0 },
        mTileCountX{ 0 },
        mTileCountZ{ 0 },
        mLodCount{ 1 },
        mLodDistances{},
        mLocalBoundingBox{},
        mHeightFieldAllocation{},
        mHeightFieldCopyFuture{},
        mHeightFieldSrvHandle{},
        mHeightFieldSrvDescriptorIndex{ 0xffffffffu },
        mHeightFieldWidth{ 0 },
        mHeightFieldHeight{ 0 },
        mMaxHeight{ 1.0f },
        mCellSizeX{ 1.0f },
        mCellSizeZ{ 1.0f },
        mOriginOffsetX{ 0.0f },
        mOriginOffsetZ{ 0.0f },
        mHeightFieldFlipV{ false },
        mStreamOriginGridX{ 0 },
        mStreamOriginGridZ{ 0 },
        mStreamWorldOriginX{ 0.0f },
        mStreamWorldOriginZ{ 0.0f },
        mHasStreamOrigin{ false } {
    }

    TerrainRenderResource::~TerrainRenderResource() {
    }

    TerrainRenderResource::TerrainRenderResource(TerrainRenderResource&& Other) noexcept
        : mModel{ std::move(Other.mModel) },
        mTileMetadata{ std::move(Other.mTileMetadata) },
        mBuildDesc{ std::move(Other.mBuildDesc) },
        mHeightFieldData{ std::move(Other.mHeightFieldData) },
        mTileQuadCount{ Other.mTileQuadCount },
        mTileCountX{ Other.mTileCountX },
        mTileCountZ{ Other.mTileCountZ },
        mLodCount{ Other.mLodCount },
        mLodDistances{ std::move(Other.mLodDistances) },
        mLocalBoundingBox{ Other.mLocalBoundingBox },
        mHeightFieldAllocation{ std::move(Other.mHeightFieldAllocation) },
        mHeightFieldCopyFuture{ std::move(Other.mHeightFieldCopyFuture) },
        mHeightFieldSrvHandle{ std::move(Other.mHeightFieldSrvHandle) },
        mHeightFieldSrvDescriptorIndex{ Other.mHeightFieldSrvDescriptorIndex },
        mHeightFieldWidth{ Other.mHeightFieldWidth },
        mHeightFieldHeight{ Other.mHeightFieldHeight },
        mMaxHeight{ Other.mMaxHeight },
        mCellSizeX{ Other.mCellSizeX },
        mCellSizeZ{ Other.mCellSizeZ },
        mOriginOffsetX{ Other.mOriginOffsetX },
        mOriginOffsetZ{ Other.mOriginOffsetZ },
        mHeightFieldFlipV{ Other.mHeightFieldFlipV },
        mStreamOriginGridX{ Other.mStreamOriginGridX },
        mStreamOriginGridZ{ Other.mStreamOriginGridZ },
        mStreamWorldOriginX{ Other.mStreamWorldOriginX },
        mStreamWorldOriginZ{ Other.mStreamWorldOriginZ },
        mHasStreamOrigin{ Other.mHasStreamOrigin } {
        Other.mTileQuadCount = 0;
        Other.mTileCountX = 0;
        Other.mTileCountZ = 0;
        Other.mLodCount = 1;
        Other.mLocalBoundingBox = DirectX::BoundingOrientedBox{};
        Other.mHeightFieldSrvDescriptorIndex = 0xffffffffu;
        Other.mHeightFieldWidth = 0;
        Other.mHeightFieldHeight = 0;
        Other.mMaxHeight = 1.0f;
        Other.mCellSizeX = 1.0f;
        Other.mCellSizeZ = 1.0f;
        Other.mOriginOffsetX = 0.0f;
        Other.mOriginOffsetZ = 0.0f;
        Other.mHeightFieldFlipV = false;
        Other.mStreamOriginGridX = 0;
        Other.mStreamOriginGridZ = 0;
        Other.mStreamWorldOriginX = 0.0f;
        Other.mStreamWorldOriginZ = 0.0f;
        Other.mHasStreamOrigin = false;
    }

    TerrainRenderResource& TerrainRenderResource::operator=(TerrainRenderResource&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mModel = std::move(Other.mModel);
        mTileMetadata = std::move(Other.mTileMetadata);
        mBuildDesc = std::move(Other.mBuildDesc);
        mHeightFieldData = std::move(Other.mHeightFieldData);
        mTileQuadCount = Other.mTileQuadCount;
        mTileCountX = Other.mTileCountX;
        mTileCountZ = Other.mTileCountZ;
        mLodCount = Other.mLodCount;
        mLodDistances = std::move(Other.mLodDistances);
        mLocalBoundingBox = Other.mLocalBoundingBox;
        mHeightFieldAllocation = std::move(Other.mHeightFieldAllocation);
        mHeightFieldCopyFuture = std::move(Other.mHeightFieldCopyFuture);
        mHeightFieldSrvHandle = std::move(Other.mHeightFieldSrvHandle);
        mHeightFieldSrvDescriptorIndex = Other.mHeightFieldSrvDescriptorIndex;
        mHeightFieldWidth = Other.mHeightFieldWidth;
        mHeightFieldHeight = Other.mHeightFieldHeight;
        mMaxHeight = Other.mMaxHeight;
        mCellSizeX = Other.mCellSizeX;
        mCellSizeZ = Other.mCellSizeZ;
        mOriginOffsetX = Other.mOriginOffsetX;
        mOriginOffsetZ = Other.mOriginOffsetZ;
        mHeightFieldFlipV = Other.mHeightFieldFlipV;
        mStreamOriginGridX = Other.mStreamOriginGridX;
        mStreamOriginGridZ = Other.mStreamOriginGridZ;
        mStreamWorldOriginX = Other.mStreamWorldOriginX;
        mStreamWorldOriginZ = Other.mStreamWorldOriginZ;
        mHasStreamOrigin = Other.mHasStreamOrigin;

        Other.mTileQuadCount = 0;
        Other.mTileCountX = 0;
        Other.mTileCountZ = 0;
        Other.mLodCount = 1;
        Other.mLocalBoundingBox = DirectX::BoundingOrientedBox{};
        Other.mHeightFieldSrvDescriptorIndex = 0xffffffffu;
        Other.mHeightFieldWidth = 0;
        Other.mHeightFieldHeight = 0;
        Other.mMaxHeight = 1.0f;
        Other.mCellSizeX = 1.0f;
        Other.mCellSizeZ = 1.0f;
        Other.mOriginOffsetX = 0.0f;
        Other.mOriginOffsetZ = 0.0f;
        Other.mHeightFieldFlipV = false;
        Other.mStreamOriginGridX = 0;
        Other.mStreamOriginGridZ = 0;
        Other.mStreamWorldOriginX = 0.0f;
        Other.mStreamWorldOriginZ = 0.0f;
        Other.mHasStreamOrigin = false;
        return *this;
    }

    void TerrainRenderResource::Initialize(std::shared_ptr<Model> ModelValue, std::vector<TerrainTileMetadata> TileMetadataValue, std::uint32_t TileQuadCountValue, std::uint32_t TileCountXValue, std::uint32_t TileCountZValue, std::uint32_t LodCountValue, std::vector<float> LodDistancesValue, const DirectX::BoundingOrientedBox& LocalBoundingBoxValue, const TerrainBuildDesc& BuildDescValue) {
        mModel = std::move(ModelValue);
        mTileMetadata = std::move(TileMetadataValue);
        mBuildDesc = BuildDescValue;
        mTileQuadCount = TileQuadCountValue;
        mTileCountX = TileCountXValue;
        mTileCountZ = TileCountZValue;
        mLodCount = LodCountValue;
        mLodDistances = std::move(LodDistancesValue);
        mLocalBoundingBox = LocalBoundingBoxValue;
        mStreamOriginGridX = BuildDescValue.mProceduralHeightFieldDesc.mSampleOffsetX;
        mStreamOriginGridZ = BuildDescValue.mProceduralHeightFieldDesc.mSampleOffsetZ;
        mStreamWorldOriginX = 0.0f;
        mStreamWorldOriginZ = 0.0f;
        mHasStreamOrigin = false;
    }

    bool TerrainRenderResource::InitializeHeightField(const HeightFieldData& Field, const TerrainBuildDesc& Desc, ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Interface::IDescriptorHeap* SrvHeap) {
        if (Device == nullptr || CopyQueue == nullptr || Allocator == nullptr || SrvHeap == nullptr || Field.HeightValues.empty() == true) {
            return false;
        }

        const std::size_t HeightFieldSizeInBytes{ Field.HeightValues.size() * sizeof(float) };
        D3D12_RESOURCE_DESC ResourceDescription{};
        ResourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ResourceDescription.Alignment = 0;
        ResourceDescription.Width = static_cast<UINT64>(HeightFieldSizeInBytes);
        ResourceDescription.Height = 1;
        ResourceDescription.DepthOrArraySize = 1;
        ResourceDescription.MipLevels = 1;
        ResourceDescription.Format = DXGI_FORMAT_UNKNOWN;
        ResourceDescription.SampleDesc.Count = 1;
        ResourceDescription.SampleDesc.Quality = 0;
        ResourceDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ResourceDescription.Flags = D3D12_RESOURCE_FLAG_NONE;

        if (Allocator->CanAllocate(ResourceDescription) == false) {
            return false;
        }

        Interface::AllocatePlacedResourceParameters AllocationParameters{ ResourceDescription, D3D12_RESOURCE_STATE_COMMON, nullptr, L"Terrain.HeightFieldBuffer" };
        mHeightFieldAllocation = Allocator->AllocatePlacedResource(AllocationParameters);
        if (mHeightFieldAllocation == nullptr || mHeightFieldAllocation->IsValid() == false) {
            mHeightFieldAllocation.reset();
            return false;
        }

        Interface::CopyQueueCopyRequest CopyRequest{};
        CopyRequest.DestinationDefaultResource = mHeightFieldAllocation->GetResource();
        CopyRequest.DestinationOffset = 0;
        CopyRequest.SourceData.resize(HeightFieldSizeInBytes);
        std::memcpy(CopyRequest.SourceData.data(), Field.HeightValues.data(), HeightFieldSizeInBytes);
        mHeightFieldCopyFuture = CopyQueue->EnqueueCopyFuture(CopyRequest);
        if (mHeightFieldCopyFuture.IsValid() == false) {
            mHeightFieldAllocation.reset();
            return false;
        }

        mHeightFieldSrvHandle = SrvHeap->Allocate();

        D3D12_SHADER_RESOURCE_VIEW_DESC ShaderResourceViewDescription{};
        ShaderResourceViewDescription.Format = DXGI_FORMAT_UNKNOWN;
        ShaderResourceViewDescription.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        ShaderResourceViewDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ShaderResourceViewDescription.Buffer.FirstElement = 0;
        ShaderResourceViewDescription.Buffer.NumElements = static_cast<UINT>(Field.HeightValues.size());
        ShaderResourceViewDescription.Buffer.StructureByteStride = sizeof(float);
        ShaderResourceViewDescription.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        Device->CreateShaderResourceView(mHeightFieldAllocation->GetResource(), &ShaderResourceViewDescription, mHeightFieldSrvHandle.GetCPU());

        mHeightFieldSrvDescriptorIndex = mHeightFieldSrvHandle.GetIndex();
        mHeightFieldWidth = Field.Width;
        mHeightFieldHeight = Field.Height;
        mMaxHeight = Desc.MaxHeight;
        mCellSizeX = Desc.CellSizeX;
        mCellSizeZ = Desc.CellSizeZ;
        mOriginOffsetX = Desc.CenterOrigin == true ? (static_cast<float>(Field.Width - 1u) * Desc.CellSizeX * 0.5f) : 0.0f;
        mOriginOffsetZ = Desc.CenterOrigin == true ? (static_cast<float>(Field.Height - 1u) * Desc.CellSizeZ * 0.5f) : 0.0f;
        mHeightFieldFlipV = Desc.FlipV;
        mBuildDesc = Desc;
        mHeightFieldData = Field;
        mStreamOriginGridX = Desc.mProceduralHeightFieldDesc.mSampleOffsetX;
        mStreamOriginGridZ = Desc.mProceduralHeightFieldDesc.mSampleOffsetZ;
        mStreamWorldOriginX = CalculateStreamingWorldOrigin(mStreamOriginGridX, Field.Width, Desc.CellSizeX, Desc.CenterOrigin);
        mStreamWorldOriginZ = CalculateStreamingWorldOrigin(mStreamOriginGridZ, Field.Height, Desc.CellSizeZ, Desc.CenterOrigin);
        mHasStreamOrigin = true;
        return true;
    }

    bool TerrainRenderResource::UpdateStreaming(const SimpleMath::Vector3& FocusPosition, ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Interface::IDescriptorHeap* SrvHeap) {
        if (IsStreamingEnabled() == false) {
            return false;
        }

        const std::uint32_t StreamingGridStep{ ResolveStreamingGridStep(mBuildDesc) };
        const std::uint32_t HeightFieldWidth{ mHeightFieldWidth > 1u ? mHeightFieldWidth : mBuildDesc.mProceduralHeightFieldDesc.mWidth };
        const std::uint32_t HeightFieldHeight{ mHeightFieldHeight > 1u ? mHeightFieldHeight : mBuildDesc.mProceduralHeightFieldDesc.mHeight };
        const std::int32_t TargetOriginGridX{ CalculateStreamingOriginGrid(FocusPosition.x, mBuildDesc.CellSizeX, HeightFieldWidth, StreamingGridStep) };
        const std::int32_t TargetOriginGridZ{ CalculateStreamingOriginGrid(FocusPosition.z, mBuildDesc.CellSizeZ, HeightFieldHeight, StreamingGridStep) };
        if (mHasStreamOrigin == true && TargetOriginGridX == mStreamOriginGridX && TargetOriginGridZ == mStreamOriginGridZ) {
            return false;
        }

        TerrainBuildDesc StreamingDesc{ mBuildDesc };
        StreamingDesc.mProceduralHeightFieldDesc.mSampleOffsetX = TargetOriginGridX;
        StreamingDesc.mProceduralHeightFieldDesc.mSampleOffsetZ = TargetOriginGridZ;

        TerrainTiledMeshData TiledMeshData{};
        HeightFieldData HeightField{};
        try {
            TerrainHeightFieldFactory HeightFieldFactory{};
            TerrainTiledMeshBuilder Builder{};
            StreamingDesc.mProceduralHeightFieldDesc = HeightFieldFactory.ResolveProceduralHeightFieldDesc(StreamingDesc);
            HeightField = HeightFieldFactory.Build(StreamingDesc);
            TiledMeshData = Builder.Build(HeightField, StreamingDesc);
        }
        catch (const std::exception&) {
            return false;
        }

        const bool IsHeightFieldUploaded{ UploadHeightFieldData(HeightField, StreamingDesc, Device, CopyQueue, Allocator, SrvHeap) };
        if (IsHeightFieldUploaded == false) {
            return false;
        }

        mTileMetadata = std::move(TiledMeshData.mTileMetadata);
        mTileQuadCount = TiledMeshData.mTileQuadCount;
        mTileCountX = TiledMeshData.mTileCountX;
        mTileCountZ = TiledMeshData.mTileCountZ;
        mLodCount = TiledMeshData.mLodCount;
        mLodDistances = std::move(TiledMeshData.mLodDistances);
        mLocalBoundingBox = TiledMeshData.mLocalBoundingBox;
        mBuildDesc = StreamingDesc;
        mStreamOriginGridX = TargetOriginGridX;
        mStreamOriginGridZ = TargetOriginGridZ;
        mStreamWorldOriginX = CalculateStreamingWorldOrigin(mStreamOriginGridX, HeightField.Width, StreamingDesc.CellSizeX, StreamingDesc.CenterOrigin);
        mStreamWorldOriginZ = CalculateStreamingWorldOrigin(mStreamOriginGridZ, HeightField.Height, StreamingDesc.CellSizeZ, StreamingDesc.CenterOrigin);
        mHasStreamOrigin = true;
        return true;
    }

    bool TerrainRenderResource::UploadHeightFieldData(const HeightFieldData& Field, const TerrainBuildDesc& Desc, ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Interface::IDescriptorHeap* SrvHeap) {
        if (Field.HeightValues.empty() == true) {
            return false;
        }

        const std::size_t HeightFieldSizeInBytes{ Field.HeightValues.size() * sizeof(float) };
        if (mHeightFieldAllocation == nullptr || mHeightFieldAllocation->IsValid() == false || mHeightFieldAllocation->GetSize() < HeightFieldSizeInBytes) {
            return InitializeHeightField(Field, Desc, Device, CopyQueue, Allocator, SrvHeap);
        }

        if (CopyQueue == nullptr || mHeightFieldCopyFuture.IsInFlight() == true) {
            return false;
        }

        Interface::CopyQueueCopyRequest CopyRequest{};
        CopyRequest.DestinationDefaultResource = mHeightFieldAllocation->GetResource();
        CopyRequest.DestinationOffset = 0;
        CopyRequest.SourceData.resize(HeightFieldSizeInBytes);
        std::memcpy(CopyRequest.SourceData.data(), Field.HeightValues.data(), HeightFieldSizeInBytes);
        mHeightFieldCopyFuture = CopyQueue->EnqueueCopyFuture(CopyRequest);
        if (mHeightFieldCopyFuture.IsValid() == false) {
            return false;
        }

        mHeightFieldData = Field;
        mHeightFieldWidth = Field.Width;
        mHeightFieldHeight = Field.Height;
        mMaxHeight = Desc.MaxHeight;
        mCellSizeX = Desc.CellSizeX;
        mCellSizeZ = Desc.CellSizeZ;
        mOriginOffsetX = Desc.CenterOrigin == true ? (static_cast<float>(Field.Width - 1u) * Desc.CellSizeX * 0.5f) : 0.0f;
        mOriginOffsetZ = Desc.CenterOrigin == true ? (static_cast<float>(Field.Height - 1u) * Desc.CellSizeZ * 0.5f) : 0.0f;
        mHeightFieldFlipV = Desc.FlipV;
        return true;
    }

    const std::shared_ptr<Model>& TerrainRenderResource::GetModel() const {
        return mModel;
    }

    const std::vector<TerrainTileMetadata>& TerrainRenderResource::GetTileMetadata() const {
        return mTileMetadata;
    }

    const TerrainBuildDesc& TerrainRenderResource::GetBuildDesc() const {
        return mBuildDesc;
    }

    const HeightFieldData& TerrainRenderResource::GetHeightFieldData() const {
        return mHeightFieldData;
    }

    std::uint32_t TerrainRenderResource::GetTileQuadCount() const {
        return mTileQuadCount;
    }

    std::uint32_t TerrainRenderResource::GetTileCountX() const {
        return mTileCountX;
    }

    std::uint32_t TerrainRenderResource::GetTileCountZ() const {
        return mTileCountZ;
    }

    std::uint32_t TerrainRenderResource::GetLodCount() const {
        return mLodCount;
    }

    const std::vector<float>& TerrainRenderResource::GetLodDistances() const {
        return mLodDistances;
    }

    float TerrainRenderResource::GetLodExponent() const {
        return mBuildDesc.mProceduralHeightFieldDesc.mLodExponent;
    }

    const DirectX::BoundingOrientedBox& TerrainRenderResource::GetLocalBoundingBox() const {
        return mLocalBoundingBox;
    }

    std::uint32_t TerrainRenderResource::GetHeightFieldSrvDescriptorIndex() const {
        return mHeightFieldSrvDescriptorIndex;
    }

    std::uint32_t TerrainRenderResource::GetHeightFieldWidth() const {
        return mHeightFieldWidth;
    }

    std::uint32_t TerrainRenderResource::GetHeightFieldHeight() const {
        return mHeightFieldHeight;
    }

    float TerrainRenderResource::GetMaxHeight() const {
        return mMaxHeight;
    }

    float TerrainRenderResource::GetCellSizeX() const {
        return mCellSizeX;
    }

    float TerrainRenderResource::GetCellSizeZ() const {
        return mCellSizeZ;
    }

    float TerrainRenderResource::GetOriginOffsetX() const {
        return mOriginOffsetX;
    }

    float TerrainRenderResource::GetOriginOffsetZ() const {
        return mOriginOffsetZ;
    }

    bool TerrainRenderResource::IsHeightFieldFlipV() const {
        return mHeightFieldFlipV;
    }

    bool TerrainRenderResource::IsStreamingEnabled() const {
        return mBuildDesc.mHeightSourceType == TerrainHeightSourceType::Procedural && mBuildDesc.mStreamingEnabled == true;
    }

    std::int32_t TerrainRenderResource::GetStreamOriginGridX() const {
        return mStreamOriginGridX;
    }

    std::int32_t TerrainRenderResource::GetStreamOriginGridZ() const {
        return mStreamOriginGridZ;
    }

    float TerrainRenderResource::GetStreamWorldOriginX() const {
        return mStreamWorldOriginX;
    }

    float TerrainRenderResource::GetStreamWorldOriginZ() const {
        return mStreamWorldOriginZ;
    }
}
