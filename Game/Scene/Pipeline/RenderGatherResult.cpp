#include "RenderGatherResult.h"
#include <iterator>
#include <utility>

namespace Game {
    namespace Pipeline {
        RenderGatherResult::RenderGatherResult()
            : mModelContexts{},
            mBoundingBoxContexts{},
            mDebugGeometryContexts{},
            mTerrainPatchContexts{},
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
            mDrawRecords.clear();
            mBonePalette.clear();

            for (RFD::ShadowRenderContext& ShadowRenderContext : mShadowRenderContexts) {
                ShadowRenderContext.ModelContexts.clear();
                ShadowRenderContext.TerrainPatchContexts.clear();
                ShadowRenderContext.DrawRecords.clear();
            }
        }

        void RenderGatherResult::Append(const RenderGatherResult& Other) {
            mModelContexts.insert(mModelContexts.end(), Other.mModelContexts.begin(), Other.mModelContexts.end());
            mBoundingBoxContexts.insert(mBoundingBoxContexts.end(), Other.mBoundingBoxContexts.begin(), Other.mBoundingBoxContexts.end());
            mDebugGeometryContexts.insert(mDebugGeometryContexts.end(), Other.mDebugGeometryContexts.begin(), Other.mDebugGeometryContexts.end());
            mTerrainPatchContexts.insert(mTerrainPatchContexts.end(), Other.mTerrainPatchContexts.begin(), Other.mTerrainPatchContexts.end());
            mDrawRecords.insert(mDrawRecords.end(), Other.mDrawRecords.begin(), Other.mDrawRecords.end());
            mBonePalette.insert(mBonePalette.end(), Other.mBonePalette.begin(), Other.mBonePalette.end());
            AppendShadowRenderContexts(Other.mShadowRenderContexts);
        }

        void RenderGatherResult::Append(RenderGatherResult&& Other) {
            mModelContexts.insert(mModelContexts.end(), std::make_move_iterator(Other.mModelContexts.begin()), std::make_move_iterator(Other.mModelContexts.end()));
            mBoundingBoxContexts.insert(mBoundingBoxContexts.end(), std::make_move_iterator(Other.mBoundingBoxContexts.begin()), std::make_move_iterator(Other.mBoundingBoxContexts.end()));
            mDebugGeometryContexts.insert(mDebugGeometryContexts.end(), std::make_move_iterator(Other.mDebugGeometryContexts.begin()), std::make_move_iterator(Other.mDebugGeometryContexts.end()));
            mTerrainPatchContexts.insert(mTerrainPatchContexts.end(), std::make_move_iterator(Other.mTerrainPatchContexts.begin()), std::make_move_iterator(Other.mTerrainPatchContexts.end()));
            mDrawRecords.insert(mDrawRecords.end(), std::make_move_iterator(Other.mDrawRecords.begin()), std::make_move_iterator(Other.mDrawRecords.end()));
            mBonePalette.insert(mBonePalette.end(), std::make_move_iterator(Other.mBonePalette.begin()), std::make_move_iterator(Other.mBonePalette.end()));
            AppendShadowRenderContexts(std::move(Other.mShadowRenderContexts));
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

        void RenderGatherResult::AppendShadowRenderContexts(const std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& OtherShadowRenderContexts) {
            for (std::size_t ShadowContextIndex{ 0 }; ShadowContextIndex < mShadowRenderContexts.size(); ++ShadowContextIndex) {
                RFD::ShadowRenderContext& TargetShadowRenderContext{ mShadowRenderContexts[ShadowContextIndex] };
                const RFD::ShadowRenderContext& SourceShadowRenderContext{ OtherShadowRenderContexts[ShadowContextIndex] };

                TargetShadowRenderContext.ModelContexts.insert(TargetShadowRenderContext.ModelContexts.end(), SourceShadowRenderContext.ModelContexts.begin(), SourceShadowRenderContext.ModelContexts.end());
                TargetShadowRenderContext.TerrainPatchContexts.insert(TargetShadowRenderContext.TerrainPatchContexts.end(), SourceShadowRenderContext.TerrainPatchContexts.begin(), SourceShadowRenderContext.TerrainPatchContexts.end());
                TargetShadowRenderContext.DrawRecords.insert(TargetShadowRenderContext.DrawRecords.end(), SourceShadowRenderContext.DrawRecords.begin(), SourceShadowRenderContext.DrawRecords.end());
            }
        }

        void RenderGatherResult::AppendShadowRenderContexts(std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>&& OtherShadowRenderContexts) {
            for (std::size_t ShadowContextIndex{ 0 }; ShadowContextIndex < mShadowRenderContexts.size(); ++ShadowContextIndex) {
                RFD::ShadowRenderContext& TargetShadowRenderContext{ mShadowRenderContexts[ShadowContextIndex] };
                RFD::ShadowRenderContext& SourceShadowRenderContext{ OtherShadowRenderContexts[ShadowContextIndex] };

                TargetShadowRenderContext.ModelContexts.insert(TargetShadowRenderContext.ModelContexts.end(), std::make_move_iterator(SourceShadowRenderContext.ModelContexts.begin()), std::make_move_iterator(SourceShadowRenderContext.ModelContexts.end()));
                TargetShadowRenderContext.TerrainPatchContexts.insert(TargetShadowRenderContext.TerrainPatchContexts.end(), std::make_move_iterator(SourceShadowRenderContext.TerrainPatchContexts.begin()), std::make_move_iterator(SourceShadowRenderContext.TerrainPatchContexts.end()));
                TargetShadowRenderContext.DrawRecords.insert(TargetShadowRenderContext.DrawRecords.end(), std::make_move_iterator(SourceShadowRenderContext.DrawRecords.begin()), std::make_move_iterator(SourceShadowRenderContext.DrawRecords.end()));
            }
        }
    }
}
