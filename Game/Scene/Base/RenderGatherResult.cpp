#include "RenderGatherResult.h"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>

namespace Game {
    namespace Pipeline {
        namespace {
            constexpr std::uint32_t InvalidRenderIndex{ 0xffffffffu };
            constexpr std::uint32_t SkinnedModelContextFlagBitMask{ 0x1u };

            std::uint32_t AddIndexOffset(std::uint32_t Index, std::size_t Offset);
            RFD::ModelContext BuildAdjustedModelContext(RFD::ModelContext ModelContext, std::size_t ModelContextOffset, std::size_t BonePaletteOffset);
            RFD::DrawRecord BuildAdjustedDrawRecord(RFD::DrawRecord DrawRecord, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset);
            bool AreShadowRenderContextsEmpty(const std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& ShadowRenderContexts);
            void AppendModelContexts(const std::vector<RFD::ModelContext>& SourceModelContexts, std::size_t ModelContextOffset, std::size_t BonePaletteOffset, std::vector<RFD::ModelContext>& OutModelContexts);
            void AppendModelContexts(std::vector<RFD::ModelContext>&& SourceModelContexts, std::size_t ModelContextOffset, std::size_t BonePaletteOffset, std::vector<RFD::ModelContext>& OutModelContexts);
            void AppendDrawRecords(const std::vector<RFD::DrawRecord>& SourceDrawRecords, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset, std::vector<RFD::DrawRecord>& OutDrawRecords);
            void AppendDrawRecords(std::vector<RFD::DrawRecord>&& SourceDrawRecords, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset, std::vector<RFD::DrawRecord>& OutDrawRecords);

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

            bool AreShadowRenderContextsEmpty(const std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& ShadowRenderContexts) {
                for (const RFD::ShadowRenderContext& ShadowRenderContext : ShadowRenderContexts) {
                    if (ShadowRenderContext.ModelContexts.empty() == false || ShadowRenderContext.TerrainPatchContexts.empty() == false || ShadowRenderContext.DrawRecords.empty() == false) {
                        return false;
                    }
                }

                return true;
            }

            void AppendModelContexts(const std::vector<RFD::ModelContext>& SourceModelContexts, std::size_t ModelContextOffset, std::size_t BonePaletteOffset, std::vector<RFD::ModelContext>& OutModelContexts) {
                OutModelContexts.reserve(OutModelContexts.size() + SourceModelContexts.size());
                for (const RFD::ModelContext& SourceModelContext : SourceModelContexts) {
                    OutModelContexts.push_back(BuildAdjustedModelContext(SourceModelContext, ModelContextOffset, BonePaletteOffset));
                }
            }

            void AppendModelContexts(std::vector<RFD::ModelContext>&& SourceModelContexts, std::size_t ModelContextOffset, std::size_t BonePaletteOffset, std::vector<RFD::ModelContext>& OutModelContexts) {
                OutModelContexts.reserve(OutModelContexts.size() + SourceModelContexts.size());
                for (RFD::ModelContext& SourceModelContext : SourceModelContexts) {
                    OutModelContexts.push_back(BuildAdjustedModelContext(std::move(SourceModelContext), ModelContextOffset, BonePaletteOffset));
                }
            }

            void AppendDrawRecords(const std::vector<RFD::DrawRecord>& SourceDrawRecords, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset, std::vector<RFD::DrawRecord>& OutDrawRecords) {
                OutDrawRecords.reserve(OutDrawRecords.size() + SourceDrawRecords.size());
                for (const RFD::DrawRecord& SourceDrawRecord : SourceDrawRecords) {
                    OutDrawRecords.push_back(BuildAdjustedDrawRecord(SourceDrawRecord, ModelContextOffset, TerrainPatchContextOffset));
                }
            }

            void AppendDrawRecords(std::vector<RFD::DrawRecord>&& SourceDrawRecords, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset, std::vector<RFD::DrawRecord>& OutDrawRecords) {
                OutDrawRecords.reserve(OutDrawRecords.size() + SourceDrawRecords.size());
                for (RFD::DrawRecord& SourceDrawRecord : SourceDrawRecords) {
                    OutDrawRecords.push_back(BuildAdjustedDrawRecord(std::move(SourceDrawRecord), ModelContextOffset, TerrainPatchContextOffset));
                }
            }
        }

        RenderGatherResult::RenderGatherResult()
            : mModelContexts{},
            mBoundingBoxContexts{},
            mDebugGeometryContexts{},
            mTerrainPatchContexts{},
            mTerrainUploadFuture{},
            mHasTerrainUploadFuture{},
            mDrawRecords{},
            mBonePalette{},
            mShadowRenderContexts{} {
        }

        RenderGatherResult::~RenderGatherResult() {
        }

        RenderGatherResult::RenderGatherResult(const RenderGatherResult& Other)
            : mModelContexts{ Other.mModelContexts },
            mBoundingBoxContexts{ Other.mBoundingBoxContexts },
            mDebugGeometryContexts{ Other.mDebugGeometryContexts },
            mTerrainPatchContexts{ Other.mTerrainPatchContexts },
            mTerrainUploadFuture{ Other.mTerrainUploadFuture },
            mHasTerrainUploadFuture{ Other.mHasTerrainUploadFuture },
            mDrawRecords{ Other.mDrawRecords },
            mBonePalette{ Other.mBonePalette },
            mShadowRenderContexts{ Other.mShadowRenderContexts } {
        }

        RenderGatherResult& RenderGatherResult::operator=(const RenderGatherResult& Other) {
            if (this == &Other) {
                return *this;
            }

            mModelContexts = Other.mModelContexts;
            mBoundingBoxContexts = Other.mBoundingBoxContexts;
            mDebugGeometryContexts = Other.mDebugGeometryContexts;
            mTerrainPatchContexts = Other.mTerrainPatchContexts;
            mTerrainUploadFuture = Other.mTerrainUploadFuture;
            mHasTerrainUploadFuture = Other.mHasTerrainUploadFuture;
            mDrawRecords = Other.mDrawRecords;
            mBonePalette = Other.mBonePalette;
            mShadowRenderContexts = Other.mShadowRenderContexts;
            return *this;
        }

        RenderGatherResult::RenderGatherResult(RenderGatherResult&& Other) noexcept
            : mModelContexts{ std::move(Other.mModelContexts) },
            mBoundingBoxContexts{ std::move(Other.mBoundingBoxContexts) },
            mDebugGeometryContexts{ std::move(Other.mDebugGeometryContexts) },
            mTerrainPatchContexts{ std::move(Other.mTerrainPatchContexts) },
            mTerrainUploadFuture{ std::move(Other.mTerrainUploadFuture) },
            mHasTerrainUploadFuture{ Other.mHasTerrainUploadFuture },
            mDrawRecords{ std::move(Other.mDrawRecords) },
            mBonePalette{ std::move(Other.mBonePalette) },
            mShadowRenderContexts{ std::move(Other.mShadowRenderContexts) } {
        }

        RenderGatherResult& RenderGatherResult::operator=(RenderGatherResult&& Other) noexcept {
            if (this == &Other) {
                return *this;
            }

            mModelContexts = std::move(Other.mModelContexts);
            mBoundingBoxContexts = std::move(Other.mBoundingBoxContexts);
            mDebugGeometryContexts = std::move(Other.mDebugGeometryContexts);
            mTerrainPatchContexts = std::move(Other.mTerrainPatchContexts);
            mTerrainUploadFuture = std::move(Other.mTerrainUploadFuture);
            mHasTerrainUploadFuture = Other.mHasTerrainUploadFuture;
            mDrawRecords = std::move(Other.mDrawRecords);
            mBonePalette = std::move(Other.mBonePalette);
            mShadowRenderContexts = std::move(Other.mShadowRenderContexts);
            return *this;
        }

        void RenderGatherResult::Clear() {
            mModelContexts.clear();
            mBoundingBoxContexts.clear();
            mDebugGeometryContexts.clear();
            mTerrainPatchContexts.clear();
            mTerrainUploadFuture = Interface::Future{};
            mHasTerrainUploadFuture = false;
            mDrawRecords.clear();
            mBonePalette.clear();

            for (RFD::ShadowRenderContext& ShadowRenderContext : mShadowRenderContexts) {
                ShadowRenderContext.ModelContexts.clear();
                ShadowRenderContext.TerrainPatchContexts.clear();
                ShadowRenderContext.DrawRecords.clear();
            }
        }

        bool RenderGatherResult::Empty() const {
            return mModelContexts.empty() == true && mBoundingBoxContexts.empty() == true && mDebugGeometryContexts.empty() == true && mTerrainPatchContexts.empty() == true && mHasTerrainUploadFuture == false && mDrawRecords.empty() == true && mBonePalette.empty() == true && AreShadowRenderContextsEmpty(mShadowRenderContexts) == true;
        }

        void RenderGatherResult::Append(const RenderGatherResult& Other) {
            const std::size_t ModelContextOffset{ mModelContexts.size() };
            const std::size_t TerrainPatchContextOffset{ mTerrainPatchContexts.size() };
            const std::size_t BonePaletteOffset{ mBonePalette.size() };

            AppendModelContexts(Other.mModelContexts, ModelContextOffset, BonePaletteOffset, mModelContexts);
            mBoundingBoxContexts.insert(mBoundingBoxContexts.end(), Other.mBoundingBoxContexts.begin(), Other.mBoundingBoxContexts.end());
            mDebugGeometryContexts.insert(mDebugGeometryContexts.end(), Other.mDebugGeometryContexts.begin(), Other.mDebugGeometryContexts.end());
            mTerrainPatchContexts.insert(mTerrainPatchContexts.end(), Other.mTerrainPatchContexts.begin(), Other.mTerrainPatchContexts.end());
            if (mHasTerrainUploadFuture == false && Other.mHasTerrainUploadFuture == true) {
                mTerrainUploadFuture = Other.mTerrainUploadFuture;
                mHasTerrainUploadFuture = true;
            }

            AppendDrawRecords(Other.mDrawRecords, ModelContextOffset, TerrainPatchContextOffset, mDrawRecords);
            mBonePalette.insert(mBonePalette.end(), Other.mBonePalette.begin(), Other.mBonePalette.end());
            AppendShadowRenderContexts(Other.mShadowRenderContexts, BonePaletteOffset);
        }

        void RenderGatherResult::Append(RenderGatherResult&& Other) {
            const std::size_t ModelContextOffset{ mModelContexts.size() };
            const std::size_t TerrainPatchContextOffset{ mTerrainPatchContexts.size() };
            const std::size_t BonePaletteOffset{ mBonePalette.size() };

            AppendModelContexts(std::move(Other.mModelContexts), ModelContextOffset, BonePaletteOffset, mModelContexts);
            mBoundingBoxContexts.insert(mBoundingBoxContexts.end(), std::make_move_iterator(Other.mBoundingBoxContexts.begin()), std::make_move_iterator(Other.mBoundingBoxContexts.end()));
            mDebugGeometryContexts.insert(mDebugGeometryContexts.end(), std::make_move_iterator(Other.mDebugGeometryContexts.begin()), std::make_move_iterator(Other.mDebugGeometryContexts.end()));
            mTerrainPatchContexts.insert(mTerrainPatchContexts.end(), std::make_move_iterator(Other.mTerrainPatchContexts.begin()), std::make_move_iterator(Other.mTerrainPatchContexts.end()));
            if (mHasTerrainUploadFuture == false && Other.mHasTerrainUploadFuture == true) {
                mTerrainUploadFuture = std::move(Other.mTerrainUploadFuture);
                mHasTerrainUploadFuture = true;
            }

            AppendDrawRecords(std::move(Other.mDrawRecords), ModelContextOffset, TerrainPatchContextOffset, mDrawRecords);
            mBonePalette.insert(mBonePalette.end(), std::make_move_iterator(Other.mBonePalette.begin()), std::make_move_iterator(Other.mBonePalette.end()));
            AppendShadowRenderContexts(std::move(Other.mShadowRenderContexts), BonePaletteOffset);
        }

        std::vector<RFD::ModelContext>& RenderGatherResult::GetModelContexts() {
            return mModelContexts;
        }

        const std::vector<RFD::ModelContext>& RenderGatherResult::GetModelContexts() const {
            return mModelContexts;
        }

        std::vector<RFD::BoundingBoxContext>& RenderGatherResult::GetBoundingBoxContexts() {
            return mBoundingBoxContexts;
        }

        const std::vector<RFD::BoundingBoxContext>& RenderGatherResult::GetBoundingBoxContexts() const {
            return mBoundingBoxContexts;
        }

        std::vector<RFD::DebugGeometryContext>& RenderGatherResult::GetDebugGeometryContexts() {
            return mDebugGeometryContexts;
        }

        const std::vector<RFD::DebugGeometryContext>& RenderGatherResult::GetDebugGeometryContexts() const {
            return mDebugGeometryContexts;
        }

        std::vector<RFD::TerrainPatchContext>& RenderGatherResult::GetTerrainPatchContexts() {
            return mTerrainPatchContexts;
        }

        const std::vector<RFD::TerrainPatchContext>& RenderGatherResult::GetTerrainPatchContexts() const {
            return mTerrainPatchContexts;
        }

        bool RenderGatherResult::HasTerrainUploadFuture() const {
            return mHasTerrainUploadFuture;
        }

        void RenderGatherResult::SetTerrainUploadFuture(const Interface::Future& TerrainUploadFuture) {
            mTerrainUploadFuture = TerrainUploadFuture;
            mHasTerrainUploadFuture = TerrainUploadFuture.IsValid();
        }

        const Interface::Future& RenderGatherResult::GetTerrainUploadFuture() const {
            return mTerrainUploadFuture;
        }

        std::vector<RFD::DrawRecord>& RenderGatherResult::GetDrawRecords() {
            return mDrawRecords;
        }

        const std::vector<RFD::DrawRecord>& RenderGatherResult::GetDrawRecords() const {
            return mDrawRecords;
        }

        std::vector<SimpleMath::Matrix>& RenderGatherResult::GetBonePalette() {
            return mBonePalette;
        }

        const std::vector<SimpleMath::Matrix>& RenderGatherResult::GetBonePalette() const {
            return mBonePalette;
        }

        std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& RenderGatherResult::GetShadowRenderContexts() {
            return mShadowRenderContexts;
        }

        const std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& RenderGatherResult::GetShadowRenderContexts() const {
            return mShadowRenderContexts;
        }

        void RenderGatherResult::AppendShadowRenderContexts(const std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& OtherShadowRenderContexts, std::size_t BonePaletteOffset) {
            for (std::size_t ShadowContextIndex{ 0 }; ShadowContextIndex < mShadowRenderContexts.size(); ++ShadowContextIndex) {
                RFD::ShadowRenderContext& TargetShadowRenderContext{ mShadowRenderContexts[ShadowContextIndex] };
                const RFD::ShadowRenderContext& SourceShadowRenderContext{ OtherShadowRenderContexts[ShadowContextIndex] };
                const std::size_t ModelContextOffset{ TargetShadowRenderContext.ModelContexts.size() };
                const std::size_t TerrainPatchContextOffset{ TargetShadowRenderContext.TerrainPatchContexts.size() };

                AppendModelContexts(SourceShadowRenderContext.ModelContexts, ModelContextOffset, BonePaletteOffset, TargetShadowRenderContext.ModelContexts);
                TargetShadowRenderContext.TerrainPatchContexts.insert(TargetShadowRenderContext.TerrainPatchContexts.end(), SourceShadowRenderContext.TerrainPatchContexts.begin(), SourceShadowRenderContext.TerrainPatchContexts.end());
                AppendDrawRecords(SourceShadowRenderContext.DrawRecords, ModelContextOffset, TerrainPatchContextOffset, TargetShadowRenderContext.DrawRecords);
            }
        }

        void RenderGatherResult::AppendShadowRenderContexts(std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>&& OtherShadowRenderContexts, std::size_t BonePaletteOffset) {
            for (std::size_t ShadowContextIndex{ 0 }; ShadowContextIndex < mShadowRenderContexts.size(); ++ShadowContextIndex) {
                RFD::ShadowRenderContext& TargetShadowRenderContext{ mShadowRenderContexts[ShadowContextIndex] };
                RFD::ShadowRenderContext& SourceShadowRenderContext{ OtherShadowRenderContexts[ShadowContextIndex] };
                const std::size_t ModelContextOffset{ TargetShadowRenderContext.ModelContexts.size() };
                const std::size_t TerrainPatchContextOffset{ TargetShadowRenderContext.TerrainPatchContexts.size() };

                AppendModelContexts(std::move(SourceShadowRenderContext.ModelContexts), ModelContextOffset, BonePaletteOffset, TargetShadowRenderContext.ModelContexts);
                TargetShadowRenderContext.TerrainPatchContexts.insert(TargetShadowRenderContext.TerrainPatchContexts.end(), std::make_move_iterator(SourceShadowRenderContext.TerrainPatchContexts.begin()), std::make_move_iterator(SourceShadowRenderContext.TerrainPatchContexts.end()));
                AppendDrawRecords(std::move(SourceShadowRenderContext.DrawRecords), ModelContextOffset, TerrainPatchContextOffset, TargetShadowRenderContext.DrawRecords);
            }
        }
    }
}
