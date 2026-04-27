#include "TerrainRenderResource.h"

#include <cstring>
#include <utility>

namespace Game {
    TerrainRenderResource::TerrainRenderResource()
        : mModel{},
        mTileMetadata{},
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
        mHeightFieldFlipV{ false } {
    }

    TerrainRenderResource::~TerrainRenderResource() {
    }

    TerrainRenderResource::TerrainRenderResource(TerrainRenderResource&& Other) noexcept
        : mModel{ std::move(Other.mModel) },
        mTileMetadata{ std::move(Other.mTileMetadata) },
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
        mHeightFieldFlipV{ Other.mHeightFieldFlipV } {
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
    }

    TerrainRenderResource& TerrainRenderResource::operator=(TerrainRenderResource&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mModel = std::move(Other.mModel);
        mTileMetadata = std::move(Other.mTileMetadata);
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
        return *this;
    }

    void TerrainRenderResource::Initialize(std::shared_ptr<Model> ModelValue, std::vector<TerrainTileMetadata> TileMetadataValue, std::uint32_t TileQuadCountValue, std::uint32_t TileCountXValue, std::uint32_t TileCountZValue, std::uint32_t LodCountValue, std::vector<float> LodDistancesValue, const DirectX::BoundingOrientedBox& LocalBoundingBoxValue) {
        mModel = std::move(ModelValue);
        mTileMetadata = std::move(TileMetadataValue);
        mTileQuadCount = TileQuadCountValue;
        mTileCountX = TileCountXValue;
        mTileCountZ = TileCountZValue;
        mLodCount = LodCountValue;
        mLodDistances = std::move(LodDistancesValue);
        mLocalBoundingBox = LocalBoundingBoxValue;
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
        return true;
    }

    const std::shared_ptr<Model>& TerrainRenderResource::GetModel() const {
        return mModel;
    }

    const std::vector<TerrainTileMetadata>& TerrainRenderResource::GetTileMetadata() const {
        return mTileMetadata;
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
}
