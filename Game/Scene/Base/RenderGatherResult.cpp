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
        }

        std::uint32_t RenderGatherResult::AddIndexOffset(std::uint32_t Index, std::size_t Offset) const {
            return Index + static_cast<std::uint32_t>(Offset);
        }

        RFD::ModelContext RenderGatherResult::BuildAdjustedModelContext(RFD::ModelContext ModelContext, std::size_t ModelContextOffset, std::size_t BonePaletteOffset) const {
            ModelContext.objectID = AddIndexOffset(ModelContext.objectID, ModelContextOffset);
            if ((ModelContext.flags & SkinnedModelContextFlagBitMask) != 0u) {
                ModelContext.boneIndexStart = AddIndexOffset(ModelContext.boneIndexStart, BonePaletteOffset);
            }

            return ModelContext;
        }

        RFD::DrawRecord RenderGatherResult::BuildAdjustedDrawRecord(RFD::DrawRecord DrawRecord, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset) const {
            DrawRecord.objectIndex = AddIndexOffset(DrawRecord.objectIndex, ModelContextOffset);
            if (DrawRecord.TerrainPatchContextIndex != InvalidRenderIndex) {
                DrawRecord.TerrainPatchContextIndex = AddIndexOffset(DrawRecord.TerrainPatchContextIndex, TerrainPatchContextOffset);
            }

            return DrawRecord;
        }

        RFD::EnvironmentDrawRecord RenderGatherResult::BuildAdjustedEnvironmentDrawRecord(RFD::EnvironmentDrawRecord DrawRecord, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset) const {
            DrawRecord.mInstanceOffset = AddIndexOffset(DrawRecord.mInstanceOffset, EnvironmentInstanceContextOffset);
            DrawRecord.mSegmentContextIndex = AddIndexOffset(DrawRecord.mSegmentContextIndex, EnvironmentSegmentContextOffset);
            return DrawRecord;
        }

        bool RenderGatherResult::AreShadowRenderContextsEmpty(const std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& ShadowRenderContexts) const {
            for (const RFD::ShadowRenderContext& ShadowRenderContext : ShadowRenderContexts) {
                if (ShadowRenderContext.ModelContexts.empty() == false || ShadowRenderContext.TerrainPatchContexts.empty() == false || ShadowRenderContext.DrawRecords.empty() == false || ShadowRenderContext.mEnvironmentDrawRecords.empty() == false) {
                    return false;
                }
            }

            return true;
        }

        void RenderGatherResult::AppendModelContexts(const std::vector<RFD::ModelContext>& SourceModelContexts, std::size_t ModelContextOffset, std::size_t BonePaletteOffset, std::vector<RFD::ModelContext>& OutModelContexts) const {
            OutModelContexts.reserve(OutModelContexts.size() + SourceModelContexts.size());
            for (const RFD::ModelContext& SourceModelContext : SourceModelContexts) {
                OutModelContexts.push_back(BuildAdjustedModelContext(SourceModelContext, ModelContextOffset, BonePaletteOffset));
            }
        }

        void RenderGatherResult::AppendModelContexts(std::vector<RFD::ModelContext>&& SourceModelContexts, std::size_t ModelContextOffset, std::size_t BonePaletteOffset, std::vector<RFD::ModelContext>& OutModelContexts) const {
            OutModelContexts.reserve(OutModelContexts.size() + SourceModelContexts.size());
            for (RFD::ModelContext& SourceModelContext : SourceModelContexts) {
                OutModelContexts.push_back(BuildAdjustedModelContext(std::move(SourceModelContext), ModelContextOffset, BonePaletteOffset));
            }
        }

        void RenderGatherResult::AppendDrawRecords(const std::vector<RFD::DrawRecord>& SourceDrawRecords, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset, std::vector<RFD::DrawRecord>& OutDrawRecords) const {
            OutDrawRecords.reserve(OutDrawRecords.size() + SourceDrawRecords.size());
            for (const RFD::DrawRecord& SourceDrawRecord : SourceDrawRecords) {
                OutDrawRecords.push_back(BuildAdjustedDrawRecord(SourceDrawRecord, ModelContextOffset, TerrainPatchContextOffset));
            }
        }

        void RenderGatherResult::AppendDrawRecords(std::vector<RFD::DrawRecord>&& SourceDrawRecords, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset, std::vector<RFD::DrawRecord>& OutDrawRecords) const {
            OutDrawRecords.reserve(OutDrawRecords.size() + SourceDrawRecords.size());
            for (RFD::DrawRecord& SourceDrawRecord : SourceDrawRecords) {
                OutDrawRecords.push_back(BuildAdjustedDrawRecord(std::move(SourceDrawRecord), ModelContextOffset, TerrainPatchContextOffset));
            }
        }

        void RenderGatherResult::AppendEnvironmentDrawRecords(const std::vector<RFD::EnvironmentDrawRecord>& SourceDrawRecords, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset, std::vector<RFD::EnvironmentDrawRecord>& OutDrawRecords) const {
            OutDrawRecords.reserve(OutDrawRecords.size() + SourceDrawRecords.size());
            for (const RFD::EnvironmentDrawRecord& SourceDrawRecord : SourceDrawRecords) {
                OutDrawRecords.push_back(BuildAdjustedEnvironmentDrawRecord(SourceDrawRecord, EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset));
            }
        }

        void RenderGatherResult::AppendEnvironmentDrawRecords(std::vector<RFD::EnvironmentDrawRecord>&& SourceDrawRecords, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset, std::vector<RFD::EnvironmentDrawRecord>& OutDrawRecords) const {
            OutDrawRecords.reserve(OutDrawRecords.size() + SourceDrawRecords.size());
            for (RFD::EnvironmentDrawRecord& SourceDrawRecord : SourceDrawRecords) {
                OutDrawRecords.push_back(BuildAdjustedEnvironmentDrawRecord(std::move(SourceDrawRecord), EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset));
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
            mEnvironmentInstanceContexts{},
            mEnvironmentSegmentContexts{},
            mEnvironmentDrawRecords{},
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
            mEnvironmentInstanceContexts{ Other.mEnvironmentInstanceContexts },
            mEnvironmentSegmentContexts{ Other.mEnvironmentSegmentContexts },
            mEnvironmentDrawRecords{ Other.mEnvironmentDrawRecords },
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
            mEnvironmentInstanceContexts = Other.mEnvironmentInstanceContexts;
            mEnvironmentSegmentContexts = Other.mEnvironmentSegmentContexts;
            mEnvironmentDrawRecords = Other.mEnvironmentDrawRecords;
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
            mEnvironmentInstanceContexts{ std::move(Other.mEnvironmentInstanceContexts) },
            mEnvironmentSegmentContexts{ std::move(Other.mEnvironmentSegmentContexts) },
            mEnvironmentDrawRecords{ std::move(Other.mEnvironmentDrawRecords) },
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
            mEnvironmentInstanceContexts = std::move(Other.mEnvironmentInstanceContexts);
            mEnvironmentSegmentContexts = std::move(Other.mEnvironmentSegmentContexts);
            mEnvironmentDrawRecords = std::move(Other.mEnvironmentDrawRecords);
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
            mEnvironmentInstanceContexts.clear();
            mEnvironmentSegmentContexts.clear();
            mEnvironmentDrawRecords.clear();

            for (RFD::ShadowRenderContext& ShadowRenderContext : mShadowRenderContexts) {
                ShadowRenderContext.ModelContexts.clear();
                ShadowRenderContext.TerrainPatchContexts.clear();
                ShadowRenderContext.DrawRecords.clear();
                ShadowRenderContext.mEnvironmentDrawRecords.clear();
            }
        }

        bool RenderGatherResult::Empty() const {
            return mModelContexts.empty() == true && mBoundingBoxContexts.empty() == true && mDebugGeometryContexts.empty() == true && mTerrainPatchContexts.empty() == true && mHasTerrainUploadFuture == false && mDrawRecords.empty() == true && mBonePalette.empty() == true && mEnvironmentInstanceContexts.empty() == true && mEnvironmentSegmentContexts.empty() == true && mEnvironmentDrawRecords.empty() == true && AreShadowRenderContextsEmpty(mShadowRenderContexts) == true;
        }

        void RenderGatherResult::Append(const RenderGatherResult& Other) {
            const std::size_t ModelContextOffset{ mModelContexts.size() };
            const std::size_t TerrainPatchContextOffset{ mTerrainPatchContexts.size() };
            const std::size_t BonePaletteOffset{ mBonePalette.size() };
            const std::size_t EnvironmentInstanceContextOffset{ mEnvironmentInstanceContexts.size() };
            const std::size_t EnvironmentSegmentContextOffset{ mEnvironmentSegmentContexts.size() };

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
            mEnvironmentInstanceContexts.insert(mEnvironmentInstanceContexts.end(), Other.mEnvironmentInstanceContexts.begin(), Other.mEnvironmentInstanceContexts.end());
            mEnvironmentSegmentContexts.insert(mEnvironmentSegmentContexts.end(), Other.mEnvironmentSegmentContexts.begin(), Other.mEnvironmentSegmentContexts.end());
            AppendEnvironmentDrawRecords(Other.mEnvironmentDrawRecords, EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset, mEnvironmentDrawRecords);
            AppendShadowRenderContexts(Other.mShadowRenderContexts, BonePaletteOffset, EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset);
        }

        void RenderGatherResult::Append(RenderGatherResult&& Other) {
            const std::size_t ModelContextOffset{ mModelContexts.size() };
            const std::size_t TerrainPatchContextOffset{ mTerrainPatchContexts.size() };
            const std::size_t BonePaletteOffset{ mBonePalette.size() };
            const std::size_t EnvironmentInstanceContextOffset{ mEnvironmentInstanceContexts.size() };
            const std::size_t EnvironmentSegmentContextOffset{ mEnvironmentSegmentContexts.size() };

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
            mEnvironmentInstanceContexts.insert(mEnvironmentInstanceContexts.end(), std::make_move_iterator(Other.mEnvironmentInstanceContexts.begin()), std::make_move_iterator(Other.mEnvironmentInstanceContexts.end()));
            mEnvironmentSegmentContexts.insert(mEnvironmentSegmentContexts.end(), std::make_move_iterator(Other.mEnvironmentSegmentContexts.begin()), std::make_move_iterator(Other.mEnvironmentSegmentContexts.end()));
            AppendEnvironmentDrawRecords(std::move(Other.mEnvironmentDrawRecords), EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset, mEnvironmentDrawRecords);
            AppendShadowRenderContexts(std::move(Other.mShadowRenderContexts), BonePaletteOffset, EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset);
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

        std::vector<RFD::EnvironmentInstanceContext>& RenderGatherResult::GetEnvironmentInstanceContexts() {
            return mEnvironmentInstanceContexts;
        }

        const std::vector<RFD::EnvironmentInstanceContext>& RenderGatherResult::GetEnvironmentInstanceContexts() const {
            return mEnvironmentInstanceContexts;
        }

        std::vector<RFD::EnvironmentSegmentContext>& RenderGatherResult::GetEnvironmentSegmentContexts() {
            return mEnvironmentSegmentContexts;
        }

        const std::vector<RFD::EnvironmentSegmentContext>& RenderGatherResult::GetEnvironmentSegmentContexts() const {
            return mEnvironmentSegmentContexts;
        }

        std::vector<RFD::EnvironmentDrawRecord>& RenderGatherResult::GetEnvironmentDrawRecords() {
            return mEnvironmentDrawRecords;
        }

        const std::vector<RFD::EnvironmentDrawRecord>& RenderGatherResult::GetEnvironmentDrawRecords() const {
            return mEnvironmentDrawRecords;
        }

        std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& RenderGatherResult::GetShadowRenderContexts() {
            return mShadowRenderContexts;
        }

        const std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& RenderGatherResult::GetShadowRenderContexts() const {
            return mShadowRenderContexts;
        }

        void RenderGatherResult::AppendShadowRenderContexts(const std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& OtherShadowRenderContexts, std::size_t BonePaletteOffset, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset) {
            for (std::size_t ShadowContextIndex{ 0 }; ShadowContextIndex < mShadowRenderContexts.size(); ++ShadowContextIndex) {
                RFD::ShadowRenderContext& TargetShadowRenderContext{ mShadowRenderContexts[ShadowContextIndex] };
                const RFD::ShadowRenderContext& SourceShadowRenderContext{ OtherShadowRenderContexts[ShadowContextIndex] };
                const std::size_t ModelContextOffset{ TargetShadowRenderContext.ModelContexts.size() };
                const std::size_t TerrainPatchContextOffset{ TargetShadowRenderContext.TerrainPatchContexts.size() };

                AppendModelContexts(SourceShadowRenderContext.ModelContexts, ModelContextOffset, BonePaletteOffset, TargetShadowRenderContext.ModelContexts);
                TargetShadowRenderContext.TerrainPatchContexts.insert(TargetShadowRenderContext.TerrainPatchContexts.end(), SourceShadowRenderContext.TerrainPatchContexts.begin(), SourceShadowRenderContext.TerrainPatchContexts.end());
                AppendDrawRecords(SourceShadowRenderContext.DrawRecords, ModelContextOffset, TerrainPatchContextOffset, TargetShadowRenderContext.DrawRecords);
                AppendEnvironmentDrawRecords(SourceShadowRenderContext.mEnvironmentDrawRecords, EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset, TargetShadowRenderContext.mEnvironmentDrawRecords);
            }
        }

        void RenderGatherResult::AppendShadowRenderContexts(std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>&& OtherShadowRenderContexts, std::size_t BonePaletteOffset, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset) {
            for (std::size_t ShadowContextIndex{ 0 }; ShadowContextIndex < mShadowRenderContexts.size(); ++ShadowContextIndex) {
                RFD::ShadowRenderContext& TargetShadowRenderContext{ mShadowRenderContexts[ShadowContextIndex] };
                RFD::ShadowRenderContext& SourceShadowRenderContext{ OtherShadowRenderContexts[ShadowContextIndex] };
                const std::size_t ModelContextOffset{ TargetShadowRenderContext.ModelContexts.size() };
                const std::size_t TerrainPatchContextOffset{ TargetShadowRenderContext.TerrainPatchContexts.size() };

                AppendModelContexts(std::move(SourceShadowRenderContext.ModelContexts), ModelContextOffset, BonePaletteOffset, TargetShadowRenderContext.ModelContexts);
                TargetShadowRenderContext.TerrainPatchContexts.insert(TargetShadowRenderContext.TerrainPatchContexts.end(), std::make_move_iterator(SourceShadowRenderContext.TerrainPatchContexts.begin()), std::make_move_iterator(SourceShadowRenderContext.TerrainPatchContexts.end()));
                AppendDrawRecords(std::move(SourceShadowRenderContext.DrawRecords), ModelContextOffset, TerrainPatchContextOffset, TargetShadowRenderContext.DrawRecords);
                AppendEnvironmentDrawRecords(std::move(SourceShadowRenderContext.mEnvironmentDrawRecords), EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset, TargetShadowRenderContext.mEnvironmentDrawRecords);
            }
        }
    }
}
