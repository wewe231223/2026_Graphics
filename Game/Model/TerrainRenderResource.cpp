#include "TerrainRenderResource.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>

#include "Terrain/TerrainSplatMapGenerator.h"

namespace {
    constexpr std::uint32_t InvalidTerrainSrvDescriptorIndex{ 0xffffffffu };

    std::uint32_t ResolveTerrainFrameIndex(std::uint32_t FrameIndex) {
        return FrameIndex % Constants::FrameCount<std::uint32_t>;
    }

    void ResetTerrainFrameBufferResource(Game::TerrainFrameBufferResource& Resource) {
        Resource.mAllocation.reset();
        Resource.mSrvHandle = Core::DX::DescriptorHandle{};
        Resource.mSrvDescriptorIndex = InvalidTerrainSrvDescriptorIndex;
    }

    void ResetTerrainFrameBufferResources(std::array<Game::TerrainFrameBufferResource, Constants::FrameCount<std::size_t>>& Resources) {
        for (Game::TerrainFrameBufferResource& Resource : Resources) {
            ResetTerrainFrameBufferResource(Resource);
        }
    }

    void ResetTerrainFrameTextureResource(Game::TerrainFrameTextureResource& Resource) {
        Resource.mAllocation.reset();
        Resource.mSrvHandle = Core::DX::DescriptorHandle{};
        Resource.mSrvDescriptorIndex = InvalidTerrainSrvDescriptorIndex;
    }

    void ResetTerrainSplatMapFrameResources(std::array<std::array<Game::TerrainFrameTextureResource, Terrain::SplatMapData::WeightMapCount>, Constants::FrameCount<std::size_t>>& Resources) {
        for (std::array<Game::TerrainFrameTextureResource, Terrain::SplatMapData::WeightMapCount>& FrameResources : Resources) {
            for (Game::TerrainFrameTextureResource& Resource : FrameResources) {
                ResetTerrainFrameTextureResource(Resource);
            }
        }
    }

    struct SplatMapMipLevelSource final {
        const std::vector<asset::Vec4>* mWeightValues{};
        std::uint32_t mWidth{};
        std::uint32_t mHeight{};
    };

    SplatMapMipLevelSource ResolveSplatMapMipLevelSource(const Terrain::SplatMapData& SplatMap, std::size_t WeightMapIndex, std::uint32_t MipLevelIndex) {
        if (MipLevelIndex == 0u) {
            return SplatMapMipLevelSource{ &SplatMap.WeightMapValues[WeightMapIndex], SplatMap.Width, SplatMap.Height };
        }

        const Terrain::SplatMapMipLevelData& MipLevel{ SplatMap.WeightMapMipLevels[WeightMapIndex][MipLevelIndex - 1u] };
        return SplatMapMipLevelSource{ &MipLevel.WeightValues, MipLevel.Width, MipLevel.Height };
    }

    std::uint16_t ResolveSplatMapMipLevelCount(const Terrain::SplatMapData& SplatMap) {
        return static_cast<std::uint16_t>(SplatMap.WeightMapMipLevels[0].size() + 1ULL);
    }

    bool IsSplatMapDataValid(const Terrain::SplatMapData& SplatMap) {
        if (SplatMap.Width == 0u || SplatMap.Height == 0u) {
            return false;
        }

        const std::size_t PixelCount{ static_cast<std::size_t>(SplatMap.Width) * static_cast<std::size_t>(SplatMap.Height) };
        const std::uint16_t MipLevelCount{ ResolveSplatMapMipLevelCount(SplatMap) };
        for (std::size_t WeightMapIndex{ 0ULL }; WeightMapIndex < Terrain::SplatMapData::WeightMapCount; ++WeightMapIndex) {
            if (SplatMap.WeightMapValues[WeightMapIndex].size() != PixelCount || SplatMap.WeightMapMipLevels[WeightMapIndex].size() + 1ULL != MipLevelCount) {
                return false;
            }

            std::uint32_t ExpectedWidth{ SplatMap.Width };
            std::uint32_t ExpectedHeight{ SplatMap.Height };
            for (std::uint32_t MipLevelIndex{ 1u }; MipLevelIndex < MipLevelCount; ++MipLevelIndex) {
                ExpectedWidth = std::max(ExpectedWidth / 2u, 1u);
                ExpectedHeight = std::max(ExpectedHeight / 2u, 1u);
                const SplatMapMipLevelSource MipLevel{ ResolveSplatMapMipLevelSource(SplatMap, WeightMapIndex, MipLevelIndex) };
                if (MipLevel.mWidth != ExpectedWidth || MipLevel.mHeight != ExpectedHeight || MipLevel.mWeightValues == nullptr || MipLevel.mWeightValues->size() != static_cast<std::size_t>(ExpectedWidth) * static_cast<std::size_t>(ExpectedHeight)) {
                    return false;
                }
            }
        }

        return true;
    }

    D3D12_RESOURCE_DESC CreateSplatMapTextureResourceDescription(const Terrain::SplatMapData& SplatMap) {
        D3D12_RESOURCE_DESC ResourceDescription{};
        ResourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        ResourceDescription.Alignment = 0;
        ResourceDescription.Width = SplatMap.Width;
        ResourceDescription.Height = SplatMap.Height;
        ResourceDescription.DepthOrArraySize = 1;
        ResourceDescription.MipLevels = ResolveSplatMapMipLevelCount(SplatMap);
        ResourceDescription.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        ResourceDescription.SampleDesc.Count = 1;
        ResourceDescription.SampleDesc.Quality = 0;
        ResourceDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        ResourceDescription.Flags = D3D12_RESOURCE_FLAG_NONE;
        return ResourceDescription;
    }

    bool IsSplatMapTextureResourceCompatible(const Game::TerrainFrameTextureResource& Resource, const D3D12_RESOURCE_DESC& ResourceDescription) {
        if (Resource.mAllocation == nullptr || Resource.mAllocation->IsValid() == false || Resource.mAllocation->GetResource() == nullptr) {
            return false;
        }

        const D3D12_RESOURCE_DESC ExistingResourceDescription{ Resource.mAllocation->GetResource()->GetDesc() };
        return ExistingResourceDescription.Dimension == ResourceDescription.Dimension && ExistingResourceDescription.Width == ResourceDescription.Width && ExistingResourceDescription.Height == ResourceDescription.Height && ExistingResourceDescription.DepthOrArraySize == ResourceDescription.DepthOrArraySize && ExistingResourceDescription.MipLevels == ResourceDescription.MipLevels && ExistingResourceDescription.Format == ResourceDescription.Format;
    }

    std::uint32_t ResolveStreamingGridStep(const Terrain::TerrainBuildDesc& Desc) {
        if (Desc.mStreamingGridStep > 0u) {
            return Desc.mStreamingGridStep;
        }

        return std::max(Desc.TileQuadCount, 1u);
    }

    std::int32_t FloorToStep(std::int32_t Value, std::uint32_t Step) {
        const std::int32_t StepValue{ static_cast<std::int32_t>(std::max(Step, 1u)) };
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
          mSplatMapData{},
          mTileQuadCount{ 0 },
          mTileCountX{ 0 },
          mTileCountZ{ 0 },
          mLodCount{ 1 },
          mLodDistances{},
          mLocalBoundingBox{},
          mHeightFieldFrameResources{},
          mSplatMapFrameResources{},
          mTerrainFrameCopyFutures{},
          mTerrainFrameDirtyFlags{},
          mHeightFieldWidth{ 0 },
          mHeightFieldHeight{ 0 },
          mSplatMapWidth{ 0 },
          mSplatMapHeight{ 0 },
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
          mStreamingBuildFuture{},
          mPendingStreamingOriginGridX{ 0 },
          mPendingStreamingOriginGridZ{ 0 },
          mHasPendingStreamingBuild{ false },
          mHasStreamOrigin{ false } {
    }

    TerrainRenderResource::~TerrainRenderResource() {
    }

    TerrainRenderResource::TerrainRenderResource(TerrainRenderResource&& Other) noexcept
        : mModel{ std::move(Other.mModel) },
        mTileMetadata{ std::move(Other.mTileMetadata) },
        mBuildDesc{ std::move(Other.mBuildDesc) },
        mHeightFieldData{ std::move(Other.mHeightFieldData) },
        mSplatMapData{ std::move(Other.mSplatMapData) },
        mTileQuadCount{ Other.mTileQuadCount },
        mTileCountX{ Other.mTileCountX },
        mTileCountZ{ Other.mTileCountZ },
        mLodCount{ Other.mLodCount },
        mLodDistances{ std::move(Other.mLodDistances) },
        mLocalBoundingBox{ Other.mLocalBoundingBox },
        mHeightFieldFrameResources{ std::move(Other.mHeightFieldFrameResources) },
        mSplatMapFrameResources{ std::move(Other.mSplatMapFrameResources) },
        mTerrainFrameCopyFutures{ std::move(Other.mTerrainFrameCopyFutures) },
        mTerrainFrameDirtyFlags{ Other.mTerrainFrameDirtyFlags },
        mHeightFieldWidth{ Other.mHeightFieldWidth },
        mHeightFieldHeight{ Other.mHeightFieldHeight },
        mSplatMapWidth{ Other.mSplatMapWidth },
        mSplatMapHeight{ Other.mSplatMapHeight },
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
        mStreamingBuildFuture{ std::move(Other.mStreamingBuildFuture) },
        mPendingStreamingOriginGridX{ Other.mPendingStreamingOriginGridX },
        mPendingStreamingOriginGridZ{ Other.mPendingStreamingOriginGridZ },
        mHasPendingStreamingBuild{ Other.mHasPendingStreamingBuild },
        mHasStreamOrigin{ Other.mHasStreamOrigin } {
        Other.mTileQuadCount = 0;
        Other.mHeightFieldData.reset();
        Other.mSplatMapData.reset();
        Other.mTileCountX = 0;
        Other.mTileCountZ = 0;
        Other.mLodCount = 1;
        Other.mLocalBoundingBox = DirectX::BoundingOrientedBox{};
        ResetTerrainFrameBufferResources(Other.mHeightFieldFrameResources);
        ResetTerrainSplatMapFrameResources(Other.mSplatMapFrameResources);
        Other.mTerrainFrameCopyFutures = {};
        Other.mTerrainFrameDirtyFlags = {};
        Other.mHeightFieldWidth = 0;
        Other.mHeightFieldHeight = 0;
        Other.mSplatMapWidth = 0;
        Other.mSplatMapHeight = 0;
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
        Other.mPendingStreamingOriginGridX = 0;
        Other.mPendingStreamingOriginGridZ = 0;
        Other.mHasPendingStreamingBuild = false;
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
        mSplatMapData = std::move(Other.mSplatMapData);
        mTileQuadCount = Other.mTileQuadCount;
        mTileCountX = Other.mTileCountX;
        mTileCountZ = Other.mTileCountZ;
        mLodCount = Other.mLodCount;
        mLodDistances = std::move(Other.mLodDistances);
        mLocalBoundingBox = Other.mLocalBoundingBox;
        mHeightFieldFrameResources = std::move(Other.mHeightFieldFrameResources);
        mSplatMapFrameResources = std::move(Other.mSplatMapFrameResources);
        mTerrainFrameCopyFutures = std::move(Other.mTerrainFrameCopyFutures);
        mTerrainFrameDirtyFlags = Other.mTerrainFrameDirtyFlags;
        mHeightFieldWidth = Other.mHeightFieldWidth;
        mHeightFieldHeight = Other.mHeightFieldHeight;
        mSplatMapWidth = Other.mSplatMapWidth;
        mSplatMapHeight = Other.mSplatMapHeight;
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
        mStreamingBuildFuture = std::move(Other.mStreamingBuildFuture);
        mPendingStreamingOriginGridX = Other.mPendingStreamingOriginGridX;
        mPendingStreamingOriginGridZ = Other.mPendingStreamingOriginGridZ;
        mHasPendingStreamingBuild = Other.mHasPendingStreamingBuild;
        mHasStreamOrigin = Other.mHasStreamOrigin;

        Other.mTileQuadCount = 0;
        Other.mHeightFieldData.reset();
        Other.mSplatMapData.reset();
        Other.mTileCountX = 0;
        Other.mTileCountZ = 0;
        Other.mLodCount = 1;
        Other.mLocalBoundingBox = DirectX::BoundingOrientedBox{};
        ResetTerrainFrameBufferResources(Other.mHeightFieldFrameResources);
        ResetTerrainSplatMapFrameResources(Other.mSplatMapFrameResources);
        Other.mTerrainFrameCopyFutures = {};
        Other.mTerrainFrameDirtyFlags = {};
        Other.mHeightFieldWidth = 0;
        Other.mHeightFieldHeight = 0;
        Other.mSplatMapWidth = 0;
        Other.mSplatMapHeight = 0;
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
        Other.mPendingStreamingOriginGridX = 0;
        Other.mPendingStreamingOriginGridZ = 0;
        Other.mHasPendingStreamingBuild = false;
        Other.mHasStreamOrigin = false;
        return *this;
    }

    void TerrainRenderResource::Initialize(std::shared_ptr<Model> ModelValue, std::vector<Terrain::TerrainTileMetadata> TileMetadataValue, std::uint32_t TileQuadCountValue, std::uint32_t TileCountXValue, std::uint32_t TileCountZValue, std::uint32_t LodCountValue, std::vector<float> LodDistancesValue, const DirectX::BoundingOrientedBox& LocalBoundingBoxValue, const Terrain::TerrainBuildDesc& BuildDescValue) {
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

    bool TerrainRenderResource::InitializeHeightField(const std::shared_ptr<const Terrain::HeightFieldData>& Field, const Terrain::TerrainBuildDesc& Desc, ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Interface::IDescriptorHeap* SrvHeap) {
        if (Device == nullptr || CopyQueue == nullptr || Allocator == nullptr || SrvHeap == nullptr || Field == nullptr || Field->HeightValues.empty() == true) {
            return false;
        }

        if (IsAnyTerrainFrameCopyInFlight() == true) {
            return false;
        }

        Terrain::TerrainSplatMapGenerator SplatMapGenerator{};
        std::shared_ptr<const Terrain::SplatMapData> SplatMap{ std::make_shared<const Terrain::SplatMapData>(SplatMapGenerator.Generate(*Field, Desc)) };
        if (SplatMap == nullptr || IsSplatMapDataValid(*SplatMap) == false) {
            return false;
        }

        for (std::uint32_t FrameIndex{ 0 }; FrameIndex < Constants::FrameCount<std::uint32_t>; ++FrameIndex) {
            const bool IsFrameUploaded{ UploadTerrainFrameData(*Field, *SplatMap, FrameIndex, Device, CopyQueue, Allocator, SrvHeap) };
            if (IsFrameUploaded == false) {
                return false;
            }
        }

        mHeightFieldData = Field;
        mSplatMapData = SplatMap;
        mBuildDesc = Desc;
        mHeightFieldWidth = Field->Width;
        mHeightFieldHeight = Field->Height;
        mSplatMapWidth = SplatMap->Width;
        mSplatMapHeight = SplatMap->Height;
        mMaxHeight = Desc.MaxHeight;
        mCellSizeX = Desc.CellSizeX;
        mCellSizeZ = Desc.CellSizeZ;
        mOriginOffsetX = Desc.CenterOrigin == true ? (static_cast<float>(Field->Width - 1u) * Desc.CellSizeX * 0.5f) : 0.0f;
        mOriginOffsetZ = Desc.CenterOrigin == true ? (static_cast<float>(Field->Height - 1u) * Desc.CellSizeZ * 0.5f) : 0.0f;
        mHeightFieldFlipV = Desc.FlipV;
        mStreamOriginGridX = Desc.mProceduralHeightFieldDesc.mSampleOffsetX;
        mStreamOriginGridZ = Desc.mProceduralHeightFieldDesc.mSampleOffsetZ;
        mStreamWorldOriginX = CalculateStreamingWorldOrigin(mStreamOriginGridX, Field->Width, Desc.CellSizeX, Desc.CenterOrigin);
        mStreamWorldOriginZ = CalculateStreamingWorldOrigin(mStreamOriginGridZ, Field->Height, Desc.CellSizeZ, Desc.CenterOrigin);
        mHasStreamOrigin = true;
        mTerrainFrameDirtyFlags.fill(false);
        return true;
    }

    bool TerrainRenderResource::UpdateStreaming(const Terrain::TerrainManager& TerrainManagerInstance, const SimpleMath::Vector3& FocusPosition, std::uint32_t FrameIndex, ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Interface::IDescriptorHeap* SrvHeap) {
        if (IsStreamingEnabled() == false) {
            return false;
        }

        const std::uint32_t StreamingGridStep{ ResolveStreamingGridStep(mBuildDesc) };
        const std::uint32_t HeightFieldWidth{ mHeightFieldWidth > 1u ? mHeightFieldWidth : mBuildDesc.mProceduralHeightFieldDesc.mWidth };
        const std::uint32_t HeightFieldHeight{ mHeightFieldHeight > 1u ? mHeightFieldHeight : mBuildDesc.mProceduralHeightFieldDesc.mHeight };
        const std::int32_t TargetOriginGridX{ CalculateStreamingOriginGrid(FocusPosition.x, mBuildDesc.CellSizeX, HeightFieldWidth, StreamingGridStep) };
        const std::int32_t TargetOriginGridZ{ CalculateStreamingOriginGrid(FocusPosition.z, mBuildDesc.CellSizeZ, HeightFieldHeight, StreamingGridStep) };

        if (mHasPendingStreamingBuild == true && mStreamingBuildFuture.valid() == true) {
            const std::future_status BuildStatus{ mStreamingBuildFuture.wait_for(std::chrono::seconds{ 0 }) };
            if (BuildStatus == std::future_status::ready) {
                const bool IsPendingResultCurrent{ mPendingStreamingOriginGridX == TargetOriginGridX && mPendingStreamingOriginGridZ == TargetOriginGridZ };
                if (IsPendingResultCurrent == true && IsTerrainFrameCopyInFlight(FrameIndex) == true) {
                    return false;
                }

                Terrain::TerrainStreamingBuildResult Result{ mStreamingBuildFuture.get() };
                mHasPendingStreamingBuild = false;
                const bool IsResultCurrent{ Result.mTargetOriginGridX == TargetOriginGridX && Result.mTargetOriginGridZ == TargetOriginGridZ };
                if (Result.mSucceeded == true && IsResultCurrent == true) {
                    return TryCommitStreamingBuild(std::move(Result), FrameIndex, Device, CopyQueue, Allocator, SrvHeap);
                }
            }
        }

        const bool IsFrameDataReady{ EnsureTerrainFrameData(FrameIndex, Device, CopyQueue, Allocator, SrvHeap) };
        if (IsFrameDataReady == false) {
            return false;
        }

        if (mHasStreamOrigin == true && TargetOriginGridX == mStreamOriginGridX && TargetOriginGridZ == mStreamOriginGridZ) {
            return false;
        }

        if (mHasPendingStreamingBuild == true) {
            return false;
        }

        Terrain::TerrainBuildDesc StreamingDesc{ mBuildDesc };
        StreamingDesc.mProceduralHeightFieldDesc.mSampleOffsetX = TargetOriginGridX;
        StreamingDesc.mProceduralHeightFieldDesc.mSampleOffsetZ = TargetOriginGridZ;
        StartStreamingBuild(TerrainManagerInstance, StreamingDesc, TargetOriginGridX, TargetOriginGridZ);
        return false;
    }

    bool TerrainRenderResource::TryCommitStreamingBuild(Terrain::TerrainStreamingBuildResult&& Result, std::uint32_t FrameIndex, ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Interface::IDescriptorHeap* SrvHeap) {
        if (Result.mSucceeded == false || Result.mHeightField == nullptr || Result.mSplatMap == nullptr || Result.mHeightField->HeightValues.empty() == true || IsSplatMapDataValid(*Result.mSplatMap) == false) {
            return false;
        }

        if (IsTerrainFrameCopyInFlight(FrameIndex) == true) {
            return false;
        }

        const bool IsFrameUploaded{ UploadTerrainFrameData(*Result.mHeightField, *Result.mSplatMap, FrameIndex, Device, CopyQueue, Allocator, SrvHeap) };
        if (IsFrameUploaded == false) {
            return false;
        }

        MarkTerrainFrameResourcesDirty();
        mTerrainFrameDirtyFlags[ResolveTerrainFrameIndex(FrameIndex)] = false;
        mHeightFieldData = Result.mHeightField;
        mSplatMapData = Result.mSplatMap;
        mTileMetadata = std::move(Result.mTileMetadata);
        mTileQuadCount = Result.mTileQuadCount;
        mTileCountX = Result.mTileCountX;
        mTileCountZ = Result.mTileCountZ;
        mLodCount = Result.mLodCount;
        mLodDistances = std::move(Result.mLodDistances);
        mLocalBoundingBox = Result.mLocalBoundingBox;
        mBuildDesc = std::move(Result.mBuildDesc);
        mHeightFieldWidth = mHeightFieldData->Width;
        mHeightFieldHeight = mHeightFieldData->Height;
        mSplatMapWidth = mSplatMapData->Width;
        mSplatMapHeight = mSplatMapData->Height;
        mMaxHeight = mBuildDesc.MaxHeight;
        mCellSizeX = mBuildDesc.CellSizeX;
        mCellSizeZ = mBuildDesc.CellSizeZ;
        mOriginOffsetX = mBuildDesc.CenterOrigin == true ? (static_cast<float>(mHeightFieldData->Width - 1u) * mBuildDesc.CellSizeX * 0.5f) : 0.0f;
        mOriginOffsetZ = mBuildDesc.CenterOrigin == true ? (static_cast<float>(mHeightFieldData->Height - 1u) * mBuildDesc.CellSizeZ * 0.5f) : 0.0f;
        mHeightFieldFlipV = mBuildDesc.FlipV;
        mStreamOriginGridX = Result.mTargetOriginGridX;
        mStreamOriginGridZ = Result.mTargetOriginGridZ;
        mStreamWorldOriginX = Result.mStreamWorldOriginX;
        mStreamWorldOriginZ = Result.mStreamWorldOriginZ;
        mHasStreamOrigin = true;
        return true;
    }

    void TerrainRenderResource::StartStreamingBuild(const Terrain::TerrainManager& TerrainManagerInstance, const Terrain::TerrainBuildDesc& StreamingDesc, std::int32_t TargetOriginGridX, std::int32_t TargetOriginGridZ) {
        try {
            mStreamingBuildFuture = std::async(std::launch::async, [&TerrainManagerInstance, StreamingDesc, TargetOriginGridX, TargetOriginGridZ]() {
                return TerrainManagerInstance.BuildStreamingData(StreamingDesc, TargetOriginGridX, TargetOriginGridZ);
            });
            mPendingStreamingOriginGridX = TargetOriginGridX;
            mPendingStreamingOriginGridZ = TargetOriginGridZ;
            mHasPendingStreamingBuild = true;
        }
        catch (const std::exception&) {
            mHasPendingStreamingBuild = false;
        }
    }

    bool TerrainRenderResource::EnsureHeightFieldFrameResource(const Terrain::HeightFieldData& Field, std::uint32_t FrameIndex, std::size_t HeightFieldSizeInBytes, ID3D12Device* Device, Interface::IGraphicsAllocator* Allocator, Interface::IDescriptorHeap* SrvHeap) {
        if (Device == nullptr || Allocator == nullptr || SrvHeap == nullptr || Field.HeightValues.empty() == true || HeightFieldSizeInBytes == 0) {
            return false;
        }

        TerrainFrameBufferResource& FrameResource{ mHeightFieldFrameResources[ResolveTerrainFrameIndex(FrameIndex)] };
        const bool IsAllocationRequired{ FrameResource.mAllocation == nullptr || FrameResource.mAllocation->IsValid() == false || FrameResource.mAllocation->GetSize() < HeightFieldSizeInBytes };
        if (IsAllocationRequired == true) {
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
            FrameResource.mAllocation = Allocator->AllocatePlacedResource(AllocationParameters);
            if (FrameResource.mAllocation == nullptr || FrameResource.mAllocation->IsValid() == false) {
                FrameResource.mAllocation.reset();
                return false;
            }
        }

        if (FrameResource.mSrvDescriptorIndex == InvalidTerrainSrvDescriptorIndex || FrameResource.mSrvHandle.IsValid() == false) {
            FrameResource.mSrvHandle = SrvHeap->Allocate();
            FrameResource.mSrvDescriptorIndex = FrameResource.mSrvHandle.GetIndex();
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC ShaderResourceViewDescription{};
        ShaderResourceViewDescription.Format = DXGI_FORMAT_UNKNOWN;
        ShaderResourceViewDescription.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        ShaderResourceViewDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ShaderResourceViewDescription.Buffer.FirstElement = 0;
        ShaderResourceViewDescription.Buffer.NumElements = static_cast<UINT>(Field.HeightValues.size());
        ShaderResourceViewDescription.Buffer.StructureByteStride = sizeof(float);
        ShaderResourceViewDescription.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        Device->CreateShaderResourceView(FrameResource.mAllocation->GetResource(), &ShaderResourceViewDescription, FrameResource.mSrvHandle.GetCPU());
        return true;
    }

    bool TerrainRenderResource::EnsureSplatMapFrameResources(const Terrain::SplatMapData& SplatMap, std::uint32_t FrameIndex, ID3D12Device* Device, Interface::IGraphicsAllocator* Allocator, Interface::IDescriptorHeap* SrvHeap) {
        if (Device == nullptr || Allocator == nullptr || SrvHeap == nullptr || IsSplatMapDataValid(SplatMap) == false) {
            return false;
        }

        const D3D12_RESOURCE_DESC ResourceDescription{ CreateSplatMapTextureResourceDescription(SplatMap) };
        if (Allocator->CanAllocate(ResourceDescription) == false) {
            return false;
        }

        std::array<TerrainFrameTextureResource, Terrain::SplatMapData::WeightMapCount>& FrameResources{ mSplatMapFrameResources[ResolveTerrainFrameIndex(FrameIndex)] };
        for (std::size_t WeightMapIndex{ 0ULL }; WeightMapIndex < Terrain::SplatMapData::WeightMapCount; ++WeightMapIndex) {
            TerrainFrameTextureResource& FrameResource{ FrameResources[WeightMapIndex] };
            if (IsSplatMapTextureResourceCompatible(FrameResource, ResourceDescription) == false) {
                const wchar_t* ResourceName{ WeightMapIndex == 0ULL ? L"Terrain.SplatMapTexture0" : L"Terrain.SplatMapTexture1" };
                Interface::AllocatePlacedResourceParameters AllocationParameters{ ResourceDescription, D3D12_RESOURCE_STATE_COMMON, nullptr, ResourceName };
                FrameResource.mAllocation = Allocator->AllocatePlacedResource(AllocationParameters);
                if (FrameResource.mAllocation == nullptr || FrameResource.mAllocation->IsValid() == false) {
                    FrameResource.mAllocation.reset();
                    return false;
                }
            }

            if (FrameResource.mSrvDescriptorIndex == InvalidTerrainSrvDescriptorIndex || FrameResource.mSrvHandle.IsValid() == false) {
                FrameResource.mSrvHandle = SrvHeap->Allocate();
                FrameResource.mSrvDescriptorIndex = FrameResource.mSrvHandle.GetIndex();
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC ShaderResourceViewDescription{};
            ShaderResourceViewDescription.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            ShaderResourceViewDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            ShaderResourceViewDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            ShaderResourceViewDescription.Texture2D.MostDetailedMip = 0;
            ShaderResourceViewDescription.Texture2D.MipLevels = ResourceDescription.MipLevels;
            ShaderResourceViewDescription.Texture2D.ResourceMinLODClamp = 0.0f;
            Device->CreateShaderResourceView(FrameResource.mAllocation->GetResource(), &ShaderResourceViewDescription, FrameResource.mSrvHandle.GetCPU());
        }

        return true;
    }

    bool TerrainRenderResource::UploadTerrainFrameData(const Terrain::HeightFieldData& Field, const Terrain::SplatMapData& SplatMap, std::uint32_t FrameIndex, ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Interface::IDescriptorHeap* SrvHeap) {
        if (Device == nullptr || CopyQueue == nullptr || Allocator == nullptr || SrvHeap == nullptr || Field.HeightValues.empty() == true || IsSplatMapDataValid(SplatMap) == false) {
            return false;
        }

        const std::uint32_t ResolvedFrameIndex{ ResolveTerrainFrameIndex(FrameIndex) };
        if (mTerrainFrameCopyFutures[ResolvedFrameIndex].IsInFlight() == true) {
            return false;
        }

        const std::size_t HeightFieldSizeInBytes{ Field.HeightValues.size() * sizeof(float) };
        const bool IsHeightFieldResourceReady{ EnsureHeightFieldFrameResource(Field, ResolvedFrameIndex, HeightFieldSizeInBytes, Device, Allocator, SrvHeap) };
        if (IsHeightFieldResourceReady == false) {
            return false;
        }

        const bool IsSplatMapResourceReady{ EnsureSplatMapFrameResources(SplatMap, ResolvedFrameIndex, Device, Allocator, SrvHeap) };
        if (IsSplatMapResourceReady == false) {
            return false;
        }

        Interface::CopyRequest HeightFieldCopyRequest{ Interface::CopyPriority::Normal };
        HeightFieldCopyRequest.DestinationDefaultResource = mHeightFieldFrameResources[ResolvedFrameIndex].mAllocation->GetResource();
        HeightFieldCopyRequest.DestinationOffset = 0;
        HeightFieldCopyRequest.SourceData = std::as_bytes(std::span<const float>{ Field.HeightValues.data(), Field.HeightValues.size() });

        RenderContract::Future HeightFieldCopyFuture{ CopyQueue->EnqueueCopyFuture(HeightFieldCopyRequest) };
        if (HeightFieldCopyFuture.IsValid() == false) {
            return false;
        }

        const D3D12_RESOURCE_DESC ResourceDescription{ CreateSplatMapTextureResourceDescription(SplatMap) };
        std::array<Interface::CopyQueueTextureCopyRequest, Terrain::SplatMapData::WeightMapCount> SplatMapCopyRequests{ Interface::CopyQueueTextureCopyRequest{ Interface::CopyPriority::Normal }, Interface::CopyQueueTextureCopyRequest{ Interface::CopyPriority::Normal } };
        for (std::size_t WeightMapIndex{ 0ULL }; WeightMapIndex < Terrain::SplatMapData::WeightMapCount; ++WeightMapIndex) {
            Interface::CopyQueueTextureCopyRequest& SplatMapCopyRequest{ SplatMapCopyRequests[WeightMapIndex] };
            SplatMapCopyRequest.DestinationTextureResource = mSplatMapFrameResources[ResolvedFrameIndex][WeightMapIndex].mAllocation->GetResource();

            const std::uint32_t MipLevelCount{ ResourceDescription.MipLevels };
            std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> SourceLayouts(MipLevelCount);
            std::vector<UINT> RowCounts(MipLevelCount);
            std::vector<UINT64> RowSizesInBytes(MipLevelCount);
            UINT64 RequiredSizeInBytes{};
            Device->GetCopyableFootprints(&ResourceDescription, 0, MipLevelCount, 0, SourceLayouts.data(), RowCounts.data(), RowSizesInBytes.data(), &RequiredSizeInBytes);
            SplatMapCopyRequest.SourceLayouts = SourceLayouts;
            SplatMapCopyRequest.SourceData.resize(static_cast<std::size_t>(RequiredSizeInBytes));

            for (std::uint32_t MipLevelIndex{ 0u }; MipLevelIndex < MipLevelCount; ++MipLevelIndex) {
                const SplatMapMipLevelSource MipLevel{ ResolveSplatMapMipLevelSource(SplatMap, WeightMapIndex, MipLevelIndex) };
                if (MipLevel.mWeightValues == nullptr) {
                    return false;
                }

                const std::size_t SourceRowSizeInBytes{ static_cast<std::size_t>(MipLevel.mWidth) * sizeof(asset::Vec4) };
                if (RowSizesInBytes[MipLevelIndex] != SourceRowSizeInBytes || RowCounts[MipLevelIndex] != MipLevel.mHeight) {
                    return false;
                }

                for (std::uint32_t RowIndex{ 0u }; RowIndex < MipLevel.mHeight; ++RowIndex) {
                    const std::byte* SourceRow{ reinterpret_cast<const std::byte*>(MipLevel.mWeightValues->data()) + (static_cast<std::size_t>(RowIndex) * SourceRowSizeInBytes) };
                    std::byte* DestinationRow{ SplatMapCopyRequest.SourceData.data() + SourceLayouts[MipLevelIndex].Offset + (static_cast<std::size_t>(RowIndex) * SourceLayouts[MipLevelIndex].Footprint.RowPitch) };
                    std::memcpy(DestinationRow, SourceRow, SourceRowSizeInBytes);
                }
            }
        }

        RenderContract::Future SplatMapCopyFuture{ CopyQueue->EnqueueTextureCopyFuture(SplatMapCopyRequests) };
        if (SplatMapCopyFuture.IsValid() == false) {
            return false;
        }

        mTerrainFrameCopyFutures[ResolvedFrameIndex] = std::move(SplatMapCopyFuture);
        return true;
    }

    bool TerrainRenderResource::EnsureTerrainFrameData(std::uint32_t FrameIndex, ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Interface::IDescriptorHeap* SrvHeap) {
        const std::uint32_t ResolvedFrameIndex{ ResolveTerrainFrameIndex(FrameIndex) };
        if (mTerrainFrameDirtyFlags[ResolvedFrameIndex] == false) {
            return true;
        }

        if (mHeightFieldData == nullptr || mSplatMapData == nullptr || mHeightFieldData->HeightValues.empty() == true || IsSplatMapDataValid(*mSplatMapData) == false) {
            return false;
        }

        const bool IsFrameUploaded{ UploadTerrainFrameData(*mHeightFieldData, *mSplatMapData, ResolvedFrameIndex, Device, CopyQueue, Allocator, SrvHeap) };
        if (IsFrameUploaded == false) {
            return false;
        }

        mTerrainFrameDirtyFlags[ResolvedFrameIndex] = false;
        return true;
    }

    bool TerrainRenderResource::IsTerrainFrameCopyInFlight(std::uint32_t FrameIndex) const {
        return mTerrainFrameCopyFutures[ResolveTerrainFrameIndex(FrameIndex)].IsInFlight();
    }

    bool TerrainRenderResource::IsAnyTerrainFrameCopyInFlight() const {
        for (const RenderContract::Future& CopyFuture : mTerrainFrameCopyFutures) {
            if (CopyFuture.IsInFlight() == true) {
                return true;
            }
        }

        return false;
    }

    void TerrainRenderResource::MarkTerrainFrameResourcesDirty() {
        mTerrainFrameDirtyFlags.fill(true);
    }

    const std::shared_ptr<Model>& TerrainRenderResource::GetModel() const {
        return mModel;
    }

    const std::vector<Terrain::TerrainTileMetadata>& TerrainRenderResource::GetTileMetadata() const {
        return mTileMetadata;
    }

    const Terrain::TerrainBuildDesc& TerrainRenderResource::GetBuildDesc() const {
        return mBuildDesc;
    }

    const Terrain::HeightFieldData& TerrainRenderResource::GetHeightFieldData() const {
        static const Terrain::HeightFieldData EmptyHeightFieldData{};
        return mHeightFieldData != nullptr ? *mHeightFieldData : EmptyHeightFieldData;
    }

    const std::shared_ptr<const Terrain::HeightFieldData>& TerrainRenderResource::GetHeightFieldDataPointer() const {
        return mHeightFieldData;
    }

    const Terrain::SplatMapData& TerrainRenderResource::GetSplatMapData() const {
        static const Terrain::SplatMapData EmptySplatMapData{};
        return mSplatMapData != nullptr ? *mSplatMapData : EmptySplatMapData;
    }

    const std::shared_ptr<const Terrain::SplatMapData>& TerrainRenderResource::GetSplatMapDataPointer() const {
        return mSplatMapData;
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

    std::uint32_t TerrainRenderResource::GetHeightFieldSrvDescriptorIndex(std::uint32_t FrameIndex) const {
        return mHeightFieldFrameResources[ResolveTerrainFrameIndex(FrameIndex)].mSrvDescriptorIndex;
    }

    std::uint32_t TerrainRenderResource::GetSplatMapSrvDescriptorIndex(std::uint32_t FrameIndex, std::uint32_t WeightMapIndex) const {
        if (WeightMapIndex >= Terrain::SplatMapData::WeightMapCount) {
            return InvalidTerrainSrvDescriptorIndex;
        }

        return mSplatMapFrameResources[ResolveTerrainFrameIndex(FrameIndex)][WeightMapIndex].mSrvDescriptorIndex;
    }

    std::uint32_t TerrainRenderResource::GetHeightFieldWidth() const {
        return mHeightFieldWidth;
    }

    std::uint32_t TerrainRenderResource::GetHeightFieldHeight() const {
        return mHeightFieldHeight;
    }

    std::uint32_t TerrainRenderResource::GetSplatMapWidth() const {
        return mSplatMapWidth;
    }

    std::uint32_t TerrainRenderResource::GetSplatMapHeight() const {
        return mSplatMapHeight;
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
        return mBuildDesc.mHeightSourceType == Terrain::TerrainHeightSourceType::Procedural && mBuildDesc.mStreamingEnabled == true;
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

    RenderContract::Future TerrainRenderResource::GetFrameUploadFuture(std::uint32_t FrameIndex) const {
        return mTerrainFrameCopyFutures[ResolveTerrainFrameIndex(FrameIndex)];
    }
}
