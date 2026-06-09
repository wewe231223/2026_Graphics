#include "RenderGatherResultMerger.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include "Game/Scene/Pipeline/RenderGatherResult.h"

namespace Game {
    namespace Pipeline {
        namespace {
            constexpr std::uint32_t InvalidRenderIndex{ 0xffffffffu };
            constexpr std::uint32_t SkinnedModelContextFlagBitMask{ 0x1u };

            std::uint32_t AddIndexOffset(std::uint32_t Index, std::size_t Offset);
            RFD::ModelContext BuildAdjustedModelContext(RFD::ModelContext ModelContext, std::size_t ModelContextOffset, std::size_t BonePaletteOffset);
            RFD::DrawRecord BuildAdjustedDrawRecord(RFD::DrawRecord DrawRecord, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset);
            void MergeModelContexts(const std::vector<RFD::ModelContext>& SourceModelContexts, std::size_t ModelContextOffset, std::size_t BonePaletteOffset, std::vector<RFD::ModelContext>& OutModelContexts);
            void MergeDrawRecords(const std::vector<RFD::DrawRecord>& SourceDrawRecords, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset, std::vector<RFD::DrawRecord>& OutDrawRecords);
            void MergeShadowRenderContexts(const std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& SourceShadowRenderContexts, std::size_t BonePaletteOffset, std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& OutShadowRenderContexts);
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

            void MergeModelContexts(const std::vector<RFD::ModelContext>& SourceModelContexts, std::size_t ModelContextOffset, std::size_t BonePaletteOffset, std::vector<RFD::ModelContext>& OutModelContexts) {
                OutModelContexts.reserve(OutModelContexts.size() + SourceModelContexts.size());
                for (const RFD::ModelContext& SourceModelContext : SourceModelContexts) {
                    OutModelContexts.push_back(BuildAdjustedModelContext(SourceModelContext, ModelContextOffset, BonePaletteOffset));
                }
            }

            void MergeDrawRecords(const std::vector<RFD::DrawRecord>& SourceDrawRecords, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset, std::vector<RFD::DrawRecord>& OutDrawRecords) {
                OutDrawRecords.reserve(OutDrawRecords.size() + SourceDrawRecords.size());
                for (const RFD::DrawRecord& SourceDrawRecord : SourceDrawRecords) {
                    OutDrawRecords.push_back(BuildAdjustedDrawRecord(SourceDrawRecord, ModelContextOffset, TerrainPatchContextOffset));
                }
            }

            void MergeShadowRenderContexts(const std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& SourceShadowRenderContexts, std::size_t BonePaletteOffset, std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& OutShadowRenderContexts) {
                for (std::size_t ShadowContextIndex{ 0 }; ShadowContextIndex < OutShadowRenderContexts.size(); ++ShadowContextIndex) {
                    const RFD::ShadowRenderContext& SourceShadowRenderContext{ SourceShadowRenderContexts[ShadowContextIndex] };
                    RFD::ShadowRenderContext& OutShadowRenderContext{ OutShadowRenderContexts[ShadowContextIndex] };
                    const std::size_t ModelContextOffset{ OutShadowRenderContext.ModelContexts.size() };
                    const std::size_t TerrainPatchContextOffset{ OutShadowRenderContext.TerrainPatchContexts.size() };

                    MergeModelContexts(SourceShadowRenderContext.ModelContexts, ModelContextOffset, BonePaletteOffset, OutShadowRenderContext.ModelContexts);
                    OutShadowRenderContext.TerrainPatchContexts.insert(OutShadowRenderContext.TerrainPatchContexts.end(), SourceShadowRenderContext.TerrainPatchContexts.begin(), SourceShadowRenderContext.TerrainPatchContexts.end());
                    MergeDrawRecords(SourceShadowRenderContext.DrawRecords, ModelContextOffset, TerrainPatchContextOffset, OutShadowRenderContext.DrawRecords);
                }
            }

            void MergeRenderGatherResult(const RenderGatherResult& SourceRenderGatherResult, RFD::RenderFrameData& OutRenderData) {
                const std::size_t ModelContextOffset{ OutRenderData.modelContexts.size() };
                const std::size_t TerrainPatchContextOffset{ OutRenderData.TerrainPatchContexts.size() };
                const std::size_t BonePaletteOffset{ OutRenderData.bonePalette.size() };

                MergeModelContexts(SourceRenderGatherResult.GetModelContexts(), ModelContextOffset, BonePaletteOffset, OutRenderData.modelContexts);
                OutRenderData.boundingBoxContexts.insert(OutRenderData.boundingBoxContexts.end(), SourceRenderGatherResult.GetBoundingBoxContexts().begin(), SourceRenderGatherResult.GetBoundingBoxContexts().end());
                OutRenderData.debugGeometryContexts.insert(OutRenderData.debugGeometryContexts.end(), SourceRenderGatherResult.GetDebugGeometryContexts().begin(), SourceRenderGatherResult.GetDebugGeometryContexts().end());
                OutRenderData.TerrainPatchContexts.insert(OutRenderData.TerrainPatchContexts.end(), SourceRenderGatherResult.GetTerrainPatchContexts().begin(), SourceRenderGatherResult.GetTerrainPatchContexts().end());
                MergeDrawRecords(SourceRenderGatherResult.GetDrawRecords(), ModelContextOffset, TerrainPatchContextOffset, OutRenderData.drawRecords);
                OutRenderData.bonePalette.insert(OutRenderData.bonePalette.end(), SourceRenderGatherResult.GetBonePalette().begin(), SourceRenderGatherResult.GetBonePalette().end());
                MergeShadowRenderContexts(SourceRenderGatherResult.GetShadowRenderContexts(), BonePaletteOffset, OutRenderData.ShadowRenderContexts);
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

        void RenderGatherResultMerger::Merge(std::span<const SceneWorkUnit> WorkUnits, RFD::RenderFrameData& OutRenderData) {
            for (const SceneWorkUnit& WorkUnit : WorkUnits) {
                const RenderGatherResult& SourceRenderGatherResult{ WorkUnit.GetRenderGatherResult() };
                if (SourceRenderGatherResult.Empty() == true) {
                    continue;
                }

                MergeRenderGatherResult(SourceRenderGatherResult, OutRenderData);
            }
        }
    }
}
