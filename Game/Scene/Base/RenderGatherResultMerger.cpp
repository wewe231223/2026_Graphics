#include "RenderGatherResultMerger.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include "Game/Scene/Base/RenderGatherResult.h"

namespace Game {
    namespace Pipeline {
        namespace {
            constexpr std::uint32_t InvalidRenderIndex{ 0xffffffffu };
            constexpr std::uint32_t SkinnedModelContextFlagBitMask{ 0x1u };

            std::uint32_t AddIndexOffset(std::uint32_t Index, std::size_t Offset);
            RFD::ModelContext BuildAdjustedModelContext(RFD::ModelContext ModelContext, std::size_t ModelContextOffset, std::size_t BonePaletteOffset);
            RFD::DrawRecord BuildAdjustedDrawRecord(RFD::DrawRecord DrawRecord, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset);
            RFD::EnvironmentDrawRecord BuildAdjustedEnvironmentDrawRecord(RFD::EnvironmentDrawRecord DrawRecord, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset);
            void MergeModelContexts(const std::vector<RFD::ModelContext>& SourceModelContexts, std::size_t ModelContextOffset, std::size_t BonePaletteOffset, std::vector<RFD::ModelContext>& OutModelContexts);
            void MergeDrawRecords(const std::vector<RFD::DrawRecord>& SourceDrawRecords, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset, std::vector<RFD::DrawRecord>& OutDrawRecords);
            void MergeEnvironmentDrawRecords(const std::vector<RFD::EnvironmentDrawRecord>& SourceDrawRecords, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset, std::vector<RFD::EnvironmentDrawRecord>& OutDrawRecords);
            void MergeShadowRenderContexts(const std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& SourceShadowRenderContexts, std::size_t BonePaletteOffset, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset, std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& OutShadowRenderContexts);
            void ReserveRenderFrameData(std::span<const RenderGatherResult> RenderGatherResults, RFD::RenderFrameData& OutRenderData);
            void MergeRenderGatherResult(const RenderGatherResult& SourceRenderGatherResult, RFD::RenderFrameData& OutRenderData);

            std::uint32_t AddIndexOffset(std::uint32_t Index, std::size_t Offset) {
                return Index + static_cast<std::uint32_t>(Offset);
            }

            RFD::ModelContext BuildAdjustedModelContext(RFD::ModelContext ModelContext, std::size_t ModelContextOffset, std::size_t BonePaletteOffset) {
                ModelContext.objectID = AddIndexOffset(ModelContext.objectID, ModelContextOffset);
                if ((ModelContext.flags & SkinnedModelContextFlagBitMask) != 0u) {
                    ModelContext.boneIndexStart = AddIndexOffset(ModelContext.boneIndexStart, BonePaletteOffset);
                }

                return ModelContext;
            }

            RFD::DrawRecord BuildAdjustedDrawRecord(RFD::DrawRecord DrawRecord, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset) {
                DrawRecord.objectIndex = AddIndexOffset(DrawRecord.objectIndex, ModelContextOffset);
                if (DrawRecord.TerrainPatchContextIndex != InvalidRenderIndex) {
                    DrawRecord.TerrainPatchContextIndex = AddIndexOffset(DrawRecord.TerrainPatchContextIndex, TerrainPatchContextOffset);
                }

                return DrawRecord;
            }

            RFD::EnvironmentDrawRecord BuildAdjustedEnvironmentDrawRecord(RFD::EnvironmentDrawRecord DrawRecord, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset) {
                DrawRecord.mInstanceOffset = AddIndexOffset(DrawRecord.mInstanceOffset, EnvironmentInstanceContextOffset);
                DrawRecord.mSegmentContextIndex = AddIndexOffset(DrawRecord.mSegmentContextIndex, EnvironmentSegmentContextOffset);
                return DrawRecord;
            }

            void MergeModelContexts(const std::vector<RFD::ModelContext>& SourceModelContexts, std::size_t ModelContextOffset, std::size_t BonePaletteOffset, std::vector<RFD::ModelContext>& OutModelContexts) {
                for (const RFD::ModelContext& SourceModelContext : SourceModelContexts) {
                    OutModelContexts.push_back(BuildAdjustedModelContext(SourceModelContext, ModelContextOffset, BonePaletteOffset));
                }
            }

            void MergeDrawRecords(const std::vector<RFD::DrawRecord>& SourceDrawRecords, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset, std::vector<RFD::DrawRecord>& OutDrawRecords) {
                for (const RFD::DrawRecord& SourceDrawRecord : SourceDrawRecords) {
                    OutDrawRecords.push_back(BuildAdjustedDrawRecord(SourceDrawRecord, ModelContextOffset, TerrainPatchContextOffset));
                }
            }

            void MergeEnvironmentDrawRecords(const std::vector<RFD::EnvironmentDrawRecord>& SourceDrawRecords, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset, std::vector<RFD::EnvironmentDrawRecord>& OutDrawRecords) {
                for (const RFD::EnvironmentDrawRecord& SourceDrawRecord : SourceDrawRecords) {
                    OutDrawRecords.push_back(BuildAdjustedEnvironmentDrawRecord(SourceDrawRecord, EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset));
                }
            }

            void MergeShadowRenderContexts(const std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& SourceShadowRenderContexts, std::size_t BonePaletteOffset, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset, std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& OutShadowRenderContexts) {
                for (std::size_t ShadowContextIndex{ 0 }; ShadowContextIndex < OutShadowRenderContexts.size(); ++ShadowContextIndex) {
                    const RFD::ShadowRenderContext& SourceShadowRenderContext{ SourceShadowRenderContexts[ShadowContextIndex] };
                    RFD::ShadowRenderContext& OutShadowRenderContext{ OutShadowRenderContexts[ShadowContextIndex] };
                    const std::size_t ModelContextOffset{ OutShadowRenderContext.ModelContexts.size() };
                    const std::size_t TerrainPatchContextOffset{ OutShadowRenderContext.TerrainPatchContexts.size() };

                    MergeModelContexts(SourceShadowRenderContext.ModelContexts, ModelContextOffset, BonePaletteOffset, OutShadowRenderContext.ModelContexts);
                    OutShadowRenderContext.TerrainPatchContexts.insert(OutShadowRenderContext.TerrainPatchContexts.end(), SourceShadowRenderContext.TerrainPatchContexts.begin(), SourceShadowRenderContext.TerrainPatchContexts.end());
                    MergeDrawRecords(SourceShadowRenderContext.DrawRecords, ModelContextOffset, TerrainPatchContextOffset, OutShadowRenderContext.DrawRecords);
                    MergeEnvironmentDrawRecords(SourceShadowRenderContext.mEnvironmentDrawRecords, EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset, OutShadowRenderContext.mEnvironmentDrawRecords);
                }
            }

            void ReserveRenderFrameData(std::span<const RenderGatherResult> RenderGatherResults, RFD::RenderFrameData& OutRenderData) {
                std::size_t ModelContextCount{ OutRenderData.modelContexts.size() };
                std::size_t BoundingBoxContextCount{ OutRenderData.boundingBoxContexts.size() };
                std::size_t DebugGeometryContextCount{ OutRenderData.debugGeometryContexts.size() };
                std::size_t TerrainPatchContextCount{ OutRenderData.TerrainPatchContexts.size() };
                std::size_t DrawRecordCount{ OutRenderData.drawRecords.size() };
                std::size_t BonePaletteCount{ OutRenderData.bonePalette.size() };
                std::size_t EnvironmentInstanceContextCount{ OutRenderData.mEnvironmentInstanceContexts.size() };
                std::size_t EnvironmentSegmentContextCount{ OutRenderData.mEnvironmentSegmentContexts.size() };
                std::size_t EnvironmentDrawRecordCount{ OutRenderData.mEnvironmentDrawRecords.size() };
                std::array<std::size_t, RFD::ShadowCascadeMaxCount> ShadowModelContextCounts{};
                std::array<std::size_t, RFD::ShadowCascadeMaxCount> ShadowTerrainPatchContextCounts{};
                std::array<std::size_t, RFD::ShadowCascadeMaxCount> ShadowDrawRecordCounts{};
                std::array<std::size_t, RFD::ShadowCascadeMaxCount> ShadowEnvironmentDrawRecordCounts{};

                for (std::size_t ShadowContextIndex{}; ShadowContextIndex < OutRenderData.ShadowRenderContexts.size(); ++ShadowContextIndex) {
                    const RFD::ShadowRenderContext& ShadowRenderContext{ OutRenderData.ShadowRenderContexts[ShadowContextIndex] };
                    ShadowModelContextCounts[ShadowContextIndex] = ShadowRenderContext.ModelContexts.size();
                    ShadowTerrainPatchContextCounts[ShadowContextIndex] = ShadowRenderContext.TerrainPatchContexts.size();
                    ShadowDrawRecordCounts[ShadowContextIndex] = ShadowRenderContext.DrawRecords.size();
                    ShadowEnvironmentDrawRecordCounts[ShadowContextIndex] = ShadowRenderContext.mEnvironmentDrawRecords.size();
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

                    const std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& SourceShadowRenderContexts{ RenderGatherResultValue.GetShadowRenderContexts() };
                    for (std::size_t ShadowContextIndex{}; ShadowContextIndex < SourceShadowRenderContexts.size(); ++ShadowContextIndex) {
                        const RFD::ShadowRenderContext& SourceShadowRenderContext{ SourceShadowRenderContexts[ShadowContextIndex] };
                        ShadowModelContextCounts[ShadowContextIndex] += SourceShadowRenderContext.ModelContexts.size();
                        ShadowTerrainPatchContextCounts[ShadowContextIndex] += SourceShadowRenderContext.TerrainPatchContexts.size();
                        ShadowDrawRecordCounts[ShadowContextIndex] += SourceShadowRenderContext.DrawRecords.size();
                        ShadowEnvironmentDrawRecordCounts[ShadowContextIndex] += SourceShadowRenderContext.mEnvironmentDrawRecords.size();
                    }
                }

                OutRenderData.modelContexts.reserve(ModelContextCount);
                OutRenderData.boundingBoxContexts.reserve(BoundingBoxContextCount);
                OutRenderData.debugGeometryContexts.reserve(DebugGeometryContextCount);
                OutRenderData.TerrainPatchContexts.reserve(TerrainPatchContextCount);
                OutRenderData.drawRecords.reserve(DrawRecordCount);
                OutRenderData.bonePalette.reserve(BonePaletteCount);
                OutRenderData.mEnvironmentInstanceContexts.reserve(EnvironmentInstanceContextCount);
                OutRenderData.mEnvironmentSegmentContexts.reserve(EnvironmentSegmentContextCount);
                OutRenderData.mEnvironmentDrawRecords.reserve(EnvironmentDrawRecordCount);

                for (std::size_t ShadowContextIndex{}; ShadowContextIndex < OutRenderData.ShadowRenderContexts.size(); ++ShadowContextIndex) {
                    RFD::ShadowRenderContext& ShadowRenderContext{ OutRenderData.ShadowRenderContexts[ShadowContextIndex] };
                    ShadowRenderContext.ModelContexts.reserve(ShadowModelContextCounts[ShadowContextIndex]);
                    ShadowRenderContext.TerrainPatchContexts.reserve(ShadowTerrainPatchContextCounts[ShadowContextIndex]);
                    ShadowRenderContext.DrawRecords.reserve(ShadowDrawRecordCounts[ShadowContextIndex]);
                    ShadowRenderContext.mEnvironmentDrawRecords.reserve(ShadowEnvironmentDrawRecordCounts[ShadowContextIndex]);
                }
            }

            void MergeRenderGatherResult(const RenderGatherResult& SourceRenderGatherResult, RFD::RenderFrameData& OutRenderData) {
                const std::size_t ModelContextOffset{ OutRenderData.modelContexts.size() };
                const std::size_t TerrainPatchContextOffset{ OutRenderData.TerrainPatchContexts.size() };
                const std::size_t BonePaletteOffset{ OutRenderData.bonePalette.size() };
                const std::size_t EnvironmentInstanceContextOffset{ OutRenderData.mEnvironmentInstanceContexts.size() };
                const std::size_t EnvironmentSegmentContextOffset{ OutRenderData.mEnvironmentSegmentContexts.size() };

                MergeModelContexts(SourceRenderGatherResult.GetModelContexts(), ModelContextOffset, BonePaletteOffset, OutRenderData.modelContexts);
                OutRenderData.boundingBoxContexts.insert(OutRenderData.boundingBoxContexts.end(), SourceRenderGatherResult.GetBoundingBoxContexts().begin(), SourceRenderGatherResult.GetBoundingBoxContexts().end());
                OutRenderData.debugGeometryContexts.insert(OutRenderData.debugGeometryContexts.end(), SourceRenderGatherResult.GetDebugGeometryContexts().begin(), SourceRenderGatherResult.GetDebugGeometryContexts().end());
                OutRenderData.TerrainPatchContexts.insert(OutRenderData.TerrainPatchContexts.end(), SourceRenderGatherResult.GetTerrainPatchContexts().begin(), SourceRenderGatherResult.GetTerrainPatchContexts().end());
                if (SourceRenderGatherResult.HasTerrainUploadFuture() == true) {
                    OutRenderData.mTerrainUploadFuture = SourceRenderGatherResult.GetTerrainUploadFuture();
                    OutRenderData.mHasTerrainUploadFuture = true;
                }

                MergeDrawRecords(SourceRenderGatherResult.GetDrawRecords(), ModelContextOffset, TerrainPatchContextOffset, OutRenderData.drawRecords);
                OutRenderData.bonePalette.insert(OutRenderData.bonePalette.end(), SourceRenderGatherResult.GetBonePalette().begin(), SourceRenderGatherResult.GetBonePalette().end());
                OutRenderData.mEnvironmentInstanceContexts.insert(OutRenderData.mEnvironmentInstanceContexts.end(), SourceRenderGatherResult.GetEnvironmentInstanceContexts().begin(), SourceRenderGatherResult.GetEnvironmentInstanceContexts().end());
                OutRenderData.mEnvironmentSegmentContexts.insert(OutRenderData.mEnvironmentSegmentContexts.end(), SourceRenderGatherResult.GetEnvironmentSegmentContexts().begin(), SourceRenderGatherResult.GetEnvironmentSegmentContexts().end());
                MergeEnvironmentDrawRecords(SourceRenderGatherResult.GetEnvironmentDrawRecords(), EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset, OutRenderData.mEnvironmentDrawRecords);
                MergeShadowRenderContexts(SourceRenderGatherResult.GetShadowRenderContexts(), BonePaletteOffset, EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset, OutRenderData.ShadowRenderContexts);
            }
        }

        RenderGatherResultMerger::RenderGatherResultMerger() {
        }

        RenderGatherResultMerger::~RenderGatherResultMerger() {
        }

        RenderGatherResultMerger::RenderGatherResultMerger(const RenderGatherResultMerger& Other) {
            (void)Other;
        }

        RenderGatherResultMerger& RenderGatherResultMerger::operator=(const RenderGatherResultMerger& Other) {
            (void)Other;
            return *this;
        }

        RenderGatherResultMerger::RenderGatherResultMerger(RenderGatherResultMerger&& Other) noexcept {
            (void)Other;
        }

        RenderGatherResultMerger& RenderGatherResultMerger::operator=(RenderGatherResultMerger&& Other) noexcept {
            (void)Other;
            return *this;
        }

        void RenderGatherResultMerger::Merge(std::span<const RenderGatherResult> RenderGatherResults, RFD::RenderFrameData& OutRenderData) {
            ReserveRenderFrameData(RenderGatherResults, OutRenderData);
            for (const RenderGatherResult& SourceRenderGatherResult : RenderGatherResults) {
                if (SourceRenderGatherResult.Empty() == true) {
                    continue;
                }

                MergeRenderGatherResult(SourceRenderGatherResult, OutRenderData);
            }
        }
    }
}
