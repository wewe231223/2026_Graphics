#include "TerrainRenderResource.h"

#include <utility>

namespace Game {
    TerrainRenderResource::TerrainRenderResource()
        : mModel{},
        mTileMetadata{},
        mTileQuadCount{ 0 },
        mTileCountX{ 0 },
        mTileCountZ{ 0 },
        mLocalBoundingBox{} {
    }

    TerrainRenderResource::~TerrainRenderResource() {
    }

    TerrainRenderResource::TerrainRenderResource(TerrainRenderResource&& Other) noexcept
        : mModel{ std::move(Other.mModel) },
        mTileMetadata{ std::move(Other.mTileMetadata) },
        mTileQuadCount{ Other.mTileQuadCount },
        mTileCountX{ Other.mTileCountX },
        mTileCountZ{ Other.mTileCountZ },
        mLocalBoundingBox{ Other.mLocalBoundingBox } {
        Other.mTileQuadCount = 0;
        Other.mTileCountX = 0;
        Other.mTileCountZ = 0;
        Other.mLocalBoundingBox = DirectX::BoundingOrientedBox{};
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
        mLocalBoundingBox = Other.mLocalBoundingBox;

        Other.mTileQuadCount = 0;
        Other.mTileCountX = 0;
        Other.mTileCountZ = 0;
        Other.mLocalBoundingBox = DirectX::BoundingOrientedBox{};
        return *this;
    }

    void TerrainRenderResource::Initialize(std::shared_ptr<Model> ModelValue, std::vector<TerrainTileMetadata> TileMetadataValue, std::uint32_t TileQuadCountValue, std::uint32_t TileCountXValue, std::uint32_t TileCountZValue, const DirectX::BoundingOrientedBox& LocalBoundingBoxValue) {
        mModel = std::move(ModelValue);
        mTileMetadata = std::move(TileMetadataValue);
        mTileQuadCount = TileQuadCountValue;
        mTileCountX = TileCountXValue;
        mTileCountZ = TileCountZValue;
        mLocalBoundingBox = LocalBoundingBoxValue;
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

    const DirectX::BoundingOrientedBox& TerrainRenderResource::GetLocalBoundingBox() const {
        return mLocalBoundingBox;
    }
}
