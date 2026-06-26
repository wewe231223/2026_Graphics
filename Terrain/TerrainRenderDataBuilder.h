#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include <DirectXCollision.h>
#include <DirectXTK12/SimpleMath.h>

#include "RenderContract/Future/Future.h"
#include "RenderContract/Writer/TerrainRenderWriter.h"
#include "Terrain/TerrainManager.h"

namespace Terrain {
    struct TerrainRenderSubMeshBinding final {
    public:
        const RenderContract::IPipeline* mPipeline{};
        std::uint32_t mMaterialIndex{};
    };

    struct TerrainRenderInput final {
    public:
        const std::vector<TerrainTileMetadata>* mTileMetadataItems{};
        const std::vector<float>* mLodDistances{};
        const RenderContract::IModelNode* mMesh{};
        const std::vector<TerrainRenderSubMeshBinding>* mSubMeshBindings{};
        RenderContract::Future mTerrainUploadFuture{};

        DirectX::BoundingOrientedBox mLocalBoundingBox{};
        DirectX::BoundingOrientedBox mParentWorldBoundingBox{};
        DirectX::SimpleMath::Matrix mWorld{};
        DirectX::SimpleMath::Matrix mPrevWorld{};
        DirectX::SimpleMath::Vector3 mCameraPosition{};
        const DirectX::BoundingFrustum* mCullingFrustum{};
        std::array<DirectX::BoundingOrientedBox, RenderContract::ShadowCascadeMaxCount> mShadowCullingBoxes{};

        std::uint32_t mTileQuadCount{};
        std::uint32_t mTileCountX{};
        std::uint32_t mTileCountZ{};
        std::uint32_t mLodCount{ 1U };
        std::uint32_t mHeightFieldWidth{};
        std::uint32_t mHeightFieldHeight{};
        std::uint32_t mSplatMapWidth{};
        std::uint32_t mSplatMapHeight{};
        std::uint32_t mHeightFieldSrvDescriptorIndex{ (std::numeric_limits<std::uint32_t>::max)() };
        std::array<std::uint32_t, SplatMapData::WeightMapCount> mSplatMapSrvDescriptorIndices{ (std::numeric_limits<std::uint32_t>::max)(), (std::numeric_limits<std::uint32_t>::max)() };
        std::uint32_t mShadowCascadeCount{};
        std::uint32_t mFrameIndex{};
        std::uint32_t mMaterialFlags{};
        std::uint32_t mPickedTileMetadataIndex{ (std::numeric_limits<std::uint32_t>::max)() };

        std::int32_t mStreamOriginGridX{};
        std::int32_t mStreamOriginGridZ{};

        float mLodExponent{};
        float mMaxHeight{ 1.0F };
        float mCellSizeX{ 1.0F };
        float mCellSizeZ{ 1.0F };
        float mOriginOffsetX{};
        float mOriginOffsetZ{};

        bool mHasCameraPosition{};
        bool mHasParentWorldBoundingBox{};
        bool mIsFrustumCullingEnabled{ true };
        bool mIsDrawBoundingBoxesEnabled{};
        bool mIsPickedParentHierarchy{};
        bool mHeightFieldFlipV{};
    };

    struct TerrainRenderResult final {
    public:
        DirectX::BoundingOrientedBox mParentWorldBoundingBox{};
        bool mHasParentWorldBoundingBox{};
    };

    TerrainRenderResult WriteTerrainRenderData(const TerrainRenderInput& Input, RenderContract::TerrainRenderWriter& Writer);
}
