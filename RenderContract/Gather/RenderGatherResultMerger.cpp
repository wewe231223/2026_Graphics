#include "RenderContract/Gather/RenderGatherResultMerger.h"

#include <array>
#include <cstddef>
#include <cstdint>

using namespace RenderContract;

namespace {
    constexpr std::uint32_t InvalidRenderIndex{ 0xffffffffu };
    constexpr std::uint32_t SkinnedModelContextFlagBitMask{ 0x1u };

    std::uint32_t AddIndexOffset(std::uint32_t Index, std::size_t Offset) {
        return Index + static_cast<std::uint32_t>(Offset);
    }

    ModelContext BuildAdjustedModelContext(ModelContext ModelContextValue, std::size_t ModelContextOffset, std::size_t BonePaletteOffset) {
        ModelContextValue.mObjectId = AddIndexOffset(ModelContextValue.mObjectId, ModelContextOffset);
        if ((ModelContextValue.mFlags & SkinnedModelContextFlagBitMask) != 0u) {
            ModelContextValue.mBoneIndexStart = AddIndexOffset(ModelContextValue.mBoneIndexStart, BonePaletteOffset);
        }

        return ModelContextValue;
    }

    DrawRecord BuildAdjustedDrawRecord(DrawRecord DrawRecordValue, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset) {
        DrawRecordValue.mObjectIndex = AddIndexOffset(DrawRecordValue.mObjectIndex, ModelContextOffset);
        if (DrawRecordValue.mTerrainPatchContextIndex != InvalidRenderIndex) {
            DrawRecordValue.mTerrainPatchContextIndex = AddIndexOffset(DrawRecordValue.mTerrainPatchContextIndex, TerrainPatchContextOffset);
        }

        return DrawRecordValue;
    }

    EnvironmentDrawRecord BuildAdjustedEnvironmentDrawRecord(EnvironmentDrawRecord DrawRecordValue, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset) {
        DrawRecordValue.mInstanceOffset = AddIndexOffset(DrawRecordValue.mInstanceOffset, EnvironmentInstanceContextOffset);
        DrawRecordValue.mSegmentContextIndex = AddIndexOffset(DrawRecordValue.mSegmentContextIndex, EnvironmentSegmentContextOffset);
        return DrawRecordValue;
    }

    void MergeModelContexts(const std::vector<ModelContext>& SourceModelContexts, std::size_t ModelContextOffset, std::size_t BonePaletteOffset, std::vector<ModelContext>& OutModelContexts) {
        for (const ModelContext& SourceModelContext : SourceModelContexts) {
            OutModelContexts.push_back(BuildAdjustedModelContext(SourceModelContext, ModelContextOffset, BonePaletteOffset));
        }
    }

    void MergeDrawRecords(const std::vector<DrawRecord>& SourceDrawRecords, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset, std::vector<DrawRecord>& OutDrawRecords) {
        for (const DrawRecord& SourceDrawRecord : SourceDrawRecords) {
            OutDrawRecords.push_back(BuildAdjustedDrawRecord(SourceDrawRecord, ModelContextOffset, TerrainPatchContextOffset));
        }
    }

    void MergeEnvironmentDrawRecords(const std::vector<EnvironmentDrawRecord>& SourceDrawRecords, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset, std::vector<EnvironmentDrawRecord>& OutDrawRecords) {
        for (const EnvironmentDrawRecord& SourceDrawRecord : SourceDrawRecords) {
            OutDrawRecords.push_back(BuildAdjustedEnvironmentDrawRecord(SourceDrawRecord, EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset));
        }
    }

    void MergeShadowRenderContexts(const std::array<ShadowRenderContext, ShadowCascadeMaxCount>& SourceShadowRenderContexts, std::size_t BonePaletteOffset, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset, std::array<ShadowRenderContext, ShadowCascadeMaxCount>& OutShadowRenderContexts) {
        for (std::size_t ShadowContextIndex{}; ShadowContextIndex < OutShadowRenderContexts.size(); ShadowContextIndex += 1ULL) {
            const ShadowRenderContext& SourceShadowRenderContext{ SourceShadowRenderContexts[ShadowContextIndex] };
            ShadowRenderContext& OutShadowRenderContext{ OutShadowRenderContexts[ShadowContextIndex] };
            const std::size_t ModelContextOffset{ OutShadowRenderContext.mModelContexts.size() };
            const std::size_t TerrainPatchContextOffset{ OutShadowRenderContext.mTerrainPatchContexts.size() };

            MergeModelContexts(SourceShadowRenderContext.mModelContexts, ModelContextOffset, BonePaletteOffset, OutShadowRenderContext.mModelContexts);
            OutShadowRenderContext.mTerrainPatchContexts.insert(OutShadowRenderContext.mTerrainPatchContexts.end(), SourceShadowRenderContext.mTerrainPatchContexts.begin(), SourceShadowRenderContext.mTerrainPatchContexts.end());
            MergeDrawRecords(SourceShadowRenderContext.mDrawRecords, ModelContextOffset, TerrainPatchContextOffset, OutShadowRenderContext.mDrawRecords);
            MergeEnvironmentDrawRecords(SourceShadowRenderContext.mEnvironmentDrawRecords, EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset, OutShadowRenderContext.mEnvironmentDrawRecords);
        }
    }

    void ReserveRenderFrameData(std::span<const RenderGatherResult> RenderGatherResults, RenderFrameData& OutRenderFrameData) {
        std::size_t ModelContextCount{ OutRenderFrameData.mModelContexts.size() };
        std::size_t BoundingBoxContextCount{ OutRenderFrameData.mBoundingBoxContexts.size() };
        std::size_t DebugGeometryContextCount{ OutRenderFrameData.mDebugGeometryContexts.size() };
        std::size_t TerrainPatchContextCount{ OutRenderFrameData.mTerrainPatchContexts.size() };
        std::size_t DrawRecordCount{ OutRenderFrameData.mDrawRecords.size() };
        std::size_t BonePaletteCount{ OutRenderFrameData.mBonePalette.size() };
        std::size_t EnvironmentInstanceContextCount{ OutRenderFrameData.mEnvironmentInstanceContexts.size() };
        std::size_t EnvironmentSegmentContextCount{ OutRenderFrameData.mEnvironmentSegmentContexts.size() };
        std::size_t EnvironmentDrawRecordCount{ OutRenderFrameData.mEnvironmentDrawRecords.size() };
        std::array<std::size_t, ShadowCascadeMaxCount> ShadowModelContextCounts{};
        std::array<std::size_t, ShadowCascadeMaxCount> ShadowTerrainPatchContextCounts{};
        std::array<std::size_t, ShadowCascadeMaxCount> ShadowDrawRecordCounts{};
        std::array<std::size_t, ShadowCascadeMaxCount> ShadowEnvironmentDrawRecordCounts{};

        for (std::size_t ShadowContextIndex{}; ShadowContextIndex < OutRenderFrameData.mShadowRenderContexts.size(); ShadowContextIndex += 1ULL) {
            const ShadowRenderContext& ShadowRenderContextValue{ OutRenderFrameData.mShadowRenderContexts[ShadowContextIndex] };
            ShadowModelContextCounts[ShadowContextIndex] = ShadowRenderContextValue.mModelContexts.size();
            ShadowTerrainPatchContextCounts[ShadowContextIndex] = ShadowRenderContextValue.mTerrainPatchContexts.size();
            ShadowDrawRecordCounts[ShadowContextIndex] = ShadowRenderContextValue.mDrawRecords.size();
            ShadowEnvironmentDrawRecordCounts[ShadowContextIndex] = ShadowRenderContextValue.mEnvironmentDrawRecords.size();
        }

        for (const RenderGatherResult& RenderGatherResultValue : RenderGatherResults) {
            ModelContextCount += RenderGatherResultValue.GetModelContexts().size();
            BoundingBoxContextCount += RenderGatherResultValue.GetBoundingBoxContexts().size();
            DebugGeometryContextCount += RenderGatherResultValue.GetDebugGeometryContexts().size();
            TerrainPatchContextCount += RenderGatherResultValue.GetTerrainPatchContexts().size();
            DrawRecordCount += RenderGatherResultValue.GetDrawRecords().size();
            BonePaletteCount += RenderGatherResultValue.GetBonePalette().size();
            EnvironmentInstanceContextCount += RenderGatherResultValue.GetEnvironmentInstanceContexts().size();
            EnvironmentSegmentContextCount += RenderGatherResultValue.GetEnvironmentSegmentContexts().size();
            EnvironmentDrawRecordCount += RenderGatherResultValue.GetEnvironmentDrawRecords().size();

            const std::array<ShadowRenderContext, ShadowCascadeMaxCount>& SourceShadowRenderContexts{ RenderGatherResultValue.GetShadowRenderContexts() };
            for (std::size_t ShadowContextIndex{}; ShadowContextIndex < SourceShadowRenderContexts.size(); ShadowContextIndex += 1ULL) {
                const ShadowRenderContext& SourceShadowRenderContext{ SourceShadowRenderContexts[ShadowContextIndex] };
                ShadowModelContextCounts[ShadowContextIndex] += SourceShadowRenderContext.mModelContexts.size();
                ShadowTerrainPatchContextCounts[ShadowContextIndex] += SourceShadowRenderContext.mTerrainPatchContexts.size();
                ShadowDrawRecordCounts[ShadowContextIndex] += SourceShadowRenderContext.mDrawRecords.size();
                ShadowEnvironmentDrawRecordCounts[ShadowContextIndex] += SourceShadowRenderContext.mEnvironmentDrawRecords.size();
            }
        }

        OutRenderFrameData.mModelContexts.reserve(ModelContextCount);
        OutRenderFrameData.mBoundingBoxContexts.reserve(BoundingBoxContextCount);
        OutRenderFrameData.mDebugGeometryContexts.reserve(DebugGeometryContextCount);
        OutRenderFrameData.mTerrainPatchContexts.reserve(TerrainPatchContextCount);
        OutRenderFrameData.mDrawRecords.reserve(DrawRecordCount);
        OutRenderFrameData.mBonePalette.reserve(BonePaletteCount);
        OutRenderFrameData.mEnvironmentInstanceContexts.reserve(EnvironmentInstanceContextCount);
        OutRenderFrameData.mEnvironmentSegmentContexts.reserve(EnvironmentSegmentContextCount);
        OutRenderFrameData.mEnvironmentDrawRecords.reserve(EnvironmentDrawRecordCount);

        for (std::size_t ShadowContextIndex{}; ShadowContextIndex < OutRenderFrameData.mShadowRenderContexts.size(); ShadowContextIndex += 1ULL) {
            ShadowRenderContext& ShadowRenderContextValue{ OutRenderFrameData.mShadowRenderContexts[ShadowContextIndex] };
            ShadowRenderContextValue.mModelContexts.reserve(ShadowModelContextCounts[ShadowContextIndex]);
            ShadowRenderContextValue.mTerrainPatchContexts.reserve(ShadowTerrainPatchContextCounts[ShadowContextIndex]);
            ShadowRenderContextValue.mDrawRecords.reserve(ShadowDrawRecordCounts[ShadowContextIndex]);
            ShadowRenderContextValue.mEnvironmentDrawRecords.reserve(ShadowEnvironmentDrawRecordCounts[ShadowContextIndex]);
        }
    }

    void MergeRenderGatherResult(const RenderGatherResult& SourceRenderGatherResult, RenderFrameData& OutRenderFrameData) {
        const std::size_t ModelContextOffset{ OutRenderFrameData.mModelContexts.size() };
        const std::size_t TerrainPatchContextOffset{ OutRenderFrameData.mTerrainPatchContexts.size() };
        const std::size_t BonePaletteOffset{ OutRenderFrameData.mBonePalette.size() };
        const std::size_t EnvironmentInstanceContextOffset{ OutRenderFrameData.mEnvironmentInstanceContexts.size() };
        const std::size_t EnvironmentSegmentContextOffset{ OutRenderFrameData.mEnvironmentSegmentContexts.size() };

        MergeModelContexts(SourceRenderGatherResult.GetModelContexts(), ModelContextOffset, BonePaletteOffset, OutRenderFrameData.mModelContexts);
        OutRenderFrameData.mBoundingBoxContexts.insert(OutRenderFrameData.mBoundingBoxContexts.end(), SourceRenderGatherResult.GetBoundingBoxContexts().begin(), SourceRenderGatherResult.GetBoundingBoxContexts().end());
        OutRenderFrameData.mDebugGeometryContexts.insert(OutRenderFrameData.mDebugGeometryContexts.end(), SourceRenderGatherResult.GetDebugGeometryContexts().begin(), SourceRenderGatherResult.GetDebugGeometryContexts().end());
        OutRenderFrameData.mTerrainPatchContexts.insert(OutRenderFrameData.mTerrainPatchContexts.end(), SourceRenderGatherResult.GetTerrainPatchContexts().begin(), SourceRenderGatherResult.GetTerrainPatchContexts().end());

        if (SourceRenderGatherResult.HasTerrainUploadFuture()) {
            OutRenderFrameData.mTerrainUploadFuture = SourceRenderGatherResult.GetTerrainUploadFuture();
            OutRenderFrameData.mHasTerrainUploadFuture = true;
        }

        MergeDrawRecords(SourceRenderGatherResult.GetDrawRecords(), ModelContextOffset, TerrainPatchContextOffset, OutRenderFrameData.mDrawRecords);
        OutRenderFrameData.mBonePalette.insert(OutRenderFrameData.mBonePalette.end(), SourceRenderGatherResult.GetBonePalette().begin(), SourceRenderGatherResult.GetBonePalette().end());
        OutRenderFrameData.mEnvironmentInstanceContexts.insert(OutRenderFrameData.mEnvironmentInstanceContexts.end(), SourceRenderGatherResult.GetEnvironmentInstanceContexts().begin(), SourceRenderGatherResult.GetEnvironmentInstanceContexts().end());
        OutRenderFrameData.mEnvironmentSegmentContexts.insert(OutRenderFrameData.mEnvironmentSegmentContexts.end(), SourceRenderGatherResult.GetEnvironmentSegmentContexts().begin(), SourceRenderGatherResult.GetEnvironmentSegmentContexts().end());
        MergeEnvironmentDrawRecords(SourceRenderGatherResult.GetEnvironmentDrawRecords(), EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset, OutRenderFrameData.mEnvironmentDrawRecords);
        MergeShadowRenderContexts(SourceRenderGatherResult.GetShadowRenderContexts(), BonePaletteOffset, EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset, OutRenderFrameData.mShadowRenderContexts);
    }
}

void RenderGatherResultMerger::Merge(std::span<const RenderGatherResult> RenderGatherResults, RenderFrameData& OutRenderFrameData) {
    ReserveRenderFrameData(RenderGatherResults, OutRenderFrameData);

    for (const RenderGatherResult& RenderGatherResultValue : RenderGatherResults) {
        if (RenderGatherResultValue.Empty() == false) {
            MergeRenderGatherResult(RenderGatherResultValue, OutRenderFrameData);
        }
    }
}
