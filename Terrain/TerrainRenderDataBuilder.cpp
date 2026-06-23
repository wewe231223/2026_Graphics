#include "Terrain/TerrainRenderDataBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace Terrain;

namespace {
    constexpr std::uint32_t PickedDrawFlagBitMask{ 0x1U };
    constexpr std::uint32_t TerrainEdgeNegativeXIndex{};
    constexpr std::uint32_t TerrainEdgeNegativeZIndex{ 1U };
    constexpr std::uint32_t TerrainEdgePositiveXIndex{ 2U };
    constexpr std::uint32_t TerrainEdgePositiveZIndex{ 3U };
    constexpr std::array<std::uint32_t, 8> TerrainTessFactorDivisors{ 1U, 2U, 4U, 8U, 16U, 32U, 64U, 128U };

    struct TerrainTileTessellationData final {
    public:
        float mBaseTessFactor{ 1.0F };
        std::array<float, 4> mOuterTessFactors{ 1.0F, 1.0F, 1.0F, 1.0F };
        std::array<float, 2> mInsideTessFactors{ 1.0F, 1.0F };
    };

    std::uint32_t CalculateTerrainTileLinearIndex(std::uint32_t TileCountX, std::uint32_t TileIndexX, std::uint32_t TileIndexZ) {
        return (TileIndexZ * TileCountX) + TileIndexX;
    }

    float CalculateTerrainTessFactor(std::uint32_t TileQuadCount, std::uint32_t LodIndex) {
        const std::uint32_t BaseFactor{ std::max(TileQuadCount, 1U) };
        const std::uint32_t DivisorIndex{ std::min(LodIndex, static_cast<std::uint32_t>(TerrainTessFactorDivisors.size() - 1U)) };
        const std::uint32_t Factor{ std::max(BaseFactor / TerrainTessFactorDivisors[DivisorIndex], 1U) };
        return static_cast<float>(Factor);
    }

    std::uint32_t SelectLodIndex(const TerrainRenderInput& Input, const TerrainTileMetadata& TileMetadata) {
        const std::size_t AvailableLodCount{ static_cast<std::size_t>(Input.mLodCount) };
        if (AvailableLodCount <= 1U || Input.mHasCameraPosition == false || Input.mLodDistances == nullptr || Input.mLodDistances->empty() == true) {
            return 0U;
        }

        const DirectX::SimpleMath::Vector3 WorldCenter{ DirectX::SimpleMath::Vector3::Transform(TileMetadata.mCenter, Input.mWorld) };
        const DirectX::SimpleMath::Vector3 CenterToCamera{ WorldCenter - Input.mCameraPosition };
        const float Distance{ std::sqrt(CenterToCamera.LengthSquared()) };
        const std::size_t TransitionCount{ std::min(Input.mLodDistances->size(), AvailableLodCount - 1ULL) };
        std::uint32_t SelectedLodIndex{};
        for (std::size_t TransitionIndex{ 0ULL }; TransitionIndex < TransitionCount; ++TransitionIndex) {
            if (Distance < (*Input.mLodDistances)[TransitionIndex]) {
                break;
            }

            SelectedLodIndex += 1U;
        }

        return SelectedLodIndex;
    }

    void SetTerrainInsideTessFactors(TerrainTileTessellationData& TessellationData) {
        TessellationData.mInsideTessFactors[0] = TessellationData.mBaseTessFactor;
        TessellationData.mInsideTessFactors[1] = TessellationData.mBaseTessFactor;
    }

    void MatchTerrainSharedEdge(TerrainTileTessellationData& FirstTessellationData, std::uint32_t FirstEdgeIndex, TerrainTileTessellationData& SecondTessellationData, std::uint32_t SecondEdgeIndex) {
        const float SharedFactor{ std::max(FirstTessellationData.mOuterTessFactors[FirstEdgeIndex], SecondTessellationData.mOuterTessFactors[SecondEdgeIndex]) };
        FirstTessellationData.mOuterTessFactors[FirstEdgeIndex] = SharedFactor;
        SecondTessellationData.mOuterTessFactors[SecondEdgeIndex] = SharedFactor;
    }

    bool IsWorldBoundingBoxVisibleByFrustum(const TerrainRenderInput& Input, const DirectX::BoundingOrientedBox& WorldBoundingBox) {
        if (Input.mIsFrustumCullingEnabled == false || Input.mCullingFrustum == nullptr) {
            return true;
        }

        return Input.mCullingFrustum->Intersects(WorldBoundingBox);
    }

    bool IsWorldBoundingBoxVisibleByShadowBox(const TerrainRenderInput& Input, const DirectX::BoundingOrientedBox& WorldBoundingBox, std::uint32_t CascadeIndex) {
        if (Input.mIsFrustumCullingEnabled == false) {
            return true;
        }

        return Input.mShadowCullingBoxes[CascadeIndex].Intersects(WorldBoundingBox);
    }

    RenderContract::TerrainPatchContext BuildTerrainPatchContext(const TerrainRenderInput& Input, const TerrainTileMetadata& TileMetadata, const TerrainTileTessellationData& TessellationData) {
        RenderContract::TerrainPatchContext PatchContext{};
        PatchContext.mOuterTessFactors = DirectX::SimpleMath::Vector4{ TessellationData.mOuterTessFactors[0], TessellationData.mOuterTessFactors[1], TessellationData.mOuterTessFactors[2], TessellationData.mOuterTessFactors[3] };
        PatchContext.mInsideTessFactors = DirectX::SimpleMath::Vector4{ TessellationData.mInsideTessFactors[0], TessellationData.mInsideTessFactors[1], 0.0F, 0.0F };
        PatchContext.mTileGrid = DirectX::SimpleMath::Vector4{ static_cast<float>(TileMetadata.mStartX), static_cast<float>(TileMetadata.mStartZ), static_cast<float>(TileMetadata.mQuadCountX), static_cast<float>(TileMetadata.mQuadCountZ) };
        PatchContext.mHeightFieldParameters = DirectX::SimpleMath::Vector4{ static_cast<float>(Input.mHeightFieldWidth), static_cast<float>(Input.mHeightFieldHeight), Input.mMaxHeight, Input.mHeightFieldFlipV == true ? 1.0F : 0.0F };
        PatchContext.mTerrainParameters = DirectX::SimpleMath::Vector4{ Input.mCellSizeX, Input.mCellSizeZ, Input.mOriginOffsetX, Input.mOriginOffsetZ };
        PatchContext.mTerrainUvParameters = DirectX::SimpleMath::Vector4{ static_cast<float>(Input.mStreamOriginGridX), static_cast<float>(Input.mStreamOriginGridZ), 0.0F, 0.0F };
        PatchContext.mHeightFieldSrvDescriptorIndex = Input.mHeightFieldSrvDescriptorIndex;
        PatchContext.mSplatMap0SrvDescriptorIndex = Input.mSplatMapSrvDescriptorIndices[0];
        PatchContext.mSplatMap1SrvDescriptorIndex = Input.mSplatMapSrvDescriptorIndices[1];
        PatchContext.mSplatMapWidth = Input.mSplatMapWidth;
        PatchContext.mSplatMapHeight = Input.mSplatMapHeight;
        return PatchContext;
    }

    void AppendBoundingBoxContext(const DirectX::BoundingOrientedBox& WorldBoundingBox, RenderContract::TerrainRenderWriter& Writer) {
        RenderContract::BoundingBoxContext BoundingBoxContext{};
        BoundingBoxContext.mCenter = DirectX::SimpleMath::Vector4{ WorldBoundingBox.Center.x, WorldBoundingBox.Center.y, WorldBoundingBox.Center.z, 1.0F };
        BoundingBoxContext.mExtents = DirectX::SimpleMath::Vector4{ WorldBoundingBox.Extents.x, WorldBoundingBox.Extents.y, WorldBoundingBox.Extents.z, 0.0F };
        BoundingBoxContext.mOrientation = DirectX::SimpleMath::Vector4{ WorldBoundingBox.Orientation.x, WorldBoundingBox.Orientation.y, WorldBoundingBox.Orientation.z, WorldBoundingBox.Orientation.w };
        Writer.GetBoundingBoxContexts().push_back(BoundingBoxContext);
    }

    void AppendTerrainDrawRecord(const TerrainRenderInput& Input, const TerrainTileMetadata& TileMetadata, const TerrainTileTessellationData& TessellationData, std::uint32_t ObjectIndex, std::uint32_t PickFlags, std::vector<RenderContract::TerrainPatchContext>& OutTerrainPatchContexts, std::vector<RenderContract::DrawRecord>& OutDrawRecords) {
        if (Input.mSubMeshBindings == nullptr || TileMetadata.mSubMeshIndex >= Input.mSubMeshBindings->size()) {
            return;
        }

        const TerrainRenderSubMeshBinding& SubMeshBinding{ (*Input.mSubMeshBindings)[TileMetadata.mSubMeshIndex] };
        RenderContract::DrawRecord DrawRecord{};
        DrawRecord.mPipeline = SubMeshBinding.mPipeline;
        DrawRecord.mMesh = Input.mMesh;
        DrawRecord.mSubMesh = TileMetadata.mSubMeshIndex;
        DrawRecord.mPass = 0U;
        DrawRecord.mObjectIndex = ObjectIndex;
        DrawRecord.mMaterialIndex = SubMeshBinding.mMaterialIndex;
        DrawRecord.mFlags = Input.mMaterialFlags | PickFlags;
        DrawRecord.mTerrainPatchContextIndex = static_cast<std::uint32_t>(OutTerrainPatchContexts.size());
        DrawRecord.mPadding0 = 0U;
        OutTerrainPatchContexts.push_back(BuildTerrainPatchContext(Input, TileMetadata, TessellationData));
        OutDrawRecords.push_back(DrawRecord);
    }
}

TerrainRenderResult Terrain::WriteTerrainRenderData(const TerrainRenderInput& Input, RenderContract::TerrainRenderWriter& Writer) {
    TerrainRenderResult Result{};
    if (Input.mTileMetadataItems == nullptr || Input.mMesh == nullptr || Input.mSubMeshBindings == nullptr || Input.mHeightFieldSrvDescriptorIndex == (std::numeric_limits<std::uint32_t>::max)() || Input.mSplatMapSrvDescriptorIndices[0] == (std::numeric_limits<std::uint32_t>::max)() || Input.mSplatMapSrvDescriptorIndices[1] == (std::numeric_limits<std::uint32_t>::max)()) {
        return Result;
    }

    if (Input.mTerrainUploadFuture.IsValid() == true) {
        Writer.SetTerrainUploadFuture(Input.mTerrainUploadFuture);
    }

    DirectX::BoundingOrientedBox ParentWorldBoundingBox{ Input.mParentWorldBoundingBox };
    if (Input.mHasParentWorldBoundingBox == false) {
        Input.mLocalBoundingBox.Transform(ParentWorldBoundingBox, Input.mWorld);
    }

    Result.mParentWorldBoundingBox = ParentWorldBoundingBox;
    Result.mHasParentWorldBoundingBox = true;

    const bool IsParentVisible{ IsWorldBoundingBoxVisibleByFrustum(Input, ParentWorldBoundingBox) };
    std::array<bool, RenderContract::ShadowCascadeMaxCount> IsParentVisibleByShadowCascade{};
    bool HasVisibleShadowParent{};
    const std::uint32_t ShadowCascadeCount{ std::min(Input.mShadowCascadeCount, RenderContract::ShadowCascadeMaxCount) };
    for (std::uint32_t CascadeIndex{}; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1U) {
        IsParentVisibleByShadowCascade[CascadeIndex] = IsWorldBoundingBoxVisibleByShadowBox(Input, ParentWorldBoundingBox, CascadeIndex);
        if (IsParentVisibleByShadowCascade[CascadeIndex] == true) {
            HasVisibleShadowParent = true;
        }
    }

    if (IsParentVisible == false && HasVisibleShadowParent == false) {
        return Result;
    }

    if (IsParentVisible == true && Input.mIsDrawBoundingBoxesEnabled == true) {
        AppendBoundingBoxContext(ParentWorldBoundingBox, Writer);
    }

    const std::vector<TerrainTileMetadata>& TileMetadataItems{ *Input.mTileMetadataItems };
    std::vector<TerrainTileTessellationData> TileTessellationItems{};
    TileTessellationItems.resize(TileMetadataItems.size());
    for (std::size_t TileMetadataIndex{}; TileMetadataIndex < TileMetadataItems.size(); TileMetadataIndex += 1U) {
        const TerrainTileMetadata& TileMetadata{ TileMetadataItems[TileMetadataIndex] };
        const std::uint32_t SelectedLodIndex{ SelectLodIndex(Input, TileMetadata) };
        const float TessFactor{ CalculateTerrainTessFactor(Input.mTileQuadCount, SelectedLodIndex) };
        TerrainTileTessellationData& TessellationData{ TileTessellationItems[TileMetadataIndex] };
        TessellationData.mBaseTessFactor = TessFactor;
        TessellationData.mOuterTessFactors[TerrainEdgeNegativeXIndex] = TessFactor;
        TessellationData.mOuterTessFactors[TerrainEdgeNegativeZIndex] = TessFactor;
        TessellationData.mOuterTessFactors[TerrainEdgePositiveXIndex] = TessFactor;
        TessellationData.mOuterTessFactors[TerrainEdgePositiveZIndex] = TessFactor;
    }

    for (std::size_t TileMetadataIndex{}; TileMetadataIndex < TileMetadataItems.size(); TileMetadataIndex += 1U) {
        const TerrainTileMetadata& TileMetadata{ TileMetadataItems[TileMetadataIndex] };
        if (TileMetadata.mTileIndexX + 1U < Input.mTileCountX) {
            const std::uint32_t NeighborIndex{ CalculateTerrainTileLinearIndex(Input.mTileCountX, TileMetadata.mTileIndexX + 1U, TileMetadata.mTileIndexZ) };
            if (NeighborIndex < TileTessellationItems.size()) {
                MatchTerrainSharedEdge(TileTessellationItems[TileMetadataIndex], TerrainEdgePositiveXIndex, TileTessellationItems[NeighborIndex], TerrainEdgeNegativeXIndex);
            }
        }

        if (TileMetadata.mTileIndexZ + 1U < Input.mTileCountZ) {
            const std::uint32_t NeighborIndex{ CalculateTerrainTileLinearIndex(Input.mTileCountX, TileMetadata.mTileIndexX, TileMetadata.mTileIndexZ + 1U) };
            if (NeighborIndex < TileTessellationItems.size()) {
                MatchTerrainSharedEdge(TileTessellationItems[TileMetadataIndex], TerrainEdgePositiveZIndex, TileTessellationItems[NeighborIndex], TerrainEdgeNegativeZIndex);
            }
        }
    }

    for (TerrainTileTessellationData& TessellationData : TileTessellationItems) {
        SetTerrainInsideTessFactors(TessellationData);
    }

    bool HasModelContext{};
    std::uint32_t ObjectIndex{};
    std::array<bool, RenderContract::ShadowCascadeMaxCount> HasShadowModelContexts{};
    std::array<std::uint32_t, RenderContract::ShadowCascadeMaxCount> ShadowObjectIndices{};
    for (std::size_t TileMetadataIndex{}; TileMetadataIndex < TileMetadataItems.size(); TileMetadataIndex += 1U) {
        const TerrainTileMetadata& TileMetadata{ TileMetadataItems[TileMetadataIndex] };
        if (TileMetadata.mSubMeshIndex >= Input.mMesh->GetSubMeshes().size()) {
            continue;
        }

        DirectX::BoundingOrientedBox TileWorldBoundingBox{};
        TileMetadata.mLocalBoundingBox.Transform(TileWorldBoundingBox, Input.mWorld);
        const bool IsVisible{ IsWorldBoundingBoxVisibleByFrustum(Input, TileWorldBoundingBox) };
        const bool IsPickedTile{ Input.mIsPickedParentHierarchy == true || Input.mPickedTileMetadataIndex == static_cast<std::uint32_t>(TileMetadataIndex) };

        if (IsVisible == true) {
            if (HasModelContext == false) {
                RenderContract::ModelContext ModelContext{};
                ModelContext.mWorld = Input.mWorld;
                ModelContext.mPrevWorld = ModelContext.mWorld;
                ModelContext.mObjectId = static_cast<std::uint32_t>(Writer.GetModelContexts().size());
                ObjectIndex = ModelContext.mObjectId;
                Writer.GetModelContexts().push_back(ModelContext);
                HasModelContext = true;
            }

            if (Input.mIsDrawBoundingBoxesEnabled == true) {
                AppendBoundingBoxContext(TileWorldBoundingBox, Writer);
            }

            AppendTerrainDrawRecord(Input, TileMetadata, TileTessellationItems[TileMetadataIndex], ObjectIndex, IsPickedTile == true ? PickedDrawFlagBitMask : 0U, Writer.GetTerrainPatchContexts(), Writer.GetDrawRecords());
        }

        for (std::uint32_t CascadeIndex{}; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1U) {
            if (IsParentVisibleByShadowCascade[CascadeIndex] == false || IsWorldBoundingBoxVisibleByShadowBox(Input, TileWorldBoundingBox, CascadeIndex) == false) {
                continue;
            }

            RenderContract::ShadowRenderContext& ShadowRenderContext{ Writer.GetShadowRenderContexts()[CascadeIndex] };
            if (HasShadowModelContexts[CascadeIndex] == false) {
                RenderContract::ModelContext ShadowModelContext{};
                ShadowModelContext.mWorld = Input.mWorld;
                ShadowModelContext.mPrevWorld = ShadowModelContext.mWorld;
                ShadowModelContext.mObjectId = static_cast<std::uint32_t>(ShadowRenderContext.mModelContexts.size());
                ShadowObjectIndices[CascadeIndex] = ShadowModelContext.mObjectId;
                ShadowRenderContext.mModelContexts.push_back(ShadowModelContext);
                HasShadowModelContexts[CascadeIndex] = true;
            }

            AppendTerrainDrawRecord(Input, TileMetadata, TileTessellationItems[TileMetadataIndex], ShadowObjectIndices[CascadeIndex], 0U, ShadowRenderContext.mTerrainPatchContexts, ShadowRenderContext.mDrawRecords);
        }
    }

    return Result;
}
