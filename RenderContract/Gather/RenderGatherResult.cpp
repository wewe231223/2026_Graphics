#include "RenderContract/Gather/RenderGatherResult.h"

#include <iterator>
#include <utility>

using namespace RenderContract;

namespace {
    constexpr std::uint32_t InvalidRenderIndex{ 0xffffffffu };
    constexpr std::uint32_t SkinnedModelContextFlagBitMask{ 0x1u };
}

void RenderGatherResult::Clear() {
    mModelContexts.clear();
    mBoundingBoxContexts.clear();
    mDebugGeometryContexts.clear();
    mTerrainPatchContexts.clear();
    mTerrainUploadFuture = Future{};
    mHasTerrainUploadFuture = false;
    mDrawRecords.clear();
    mBonePalette.clear();
    mEnvironmentInstanceContexts.clear();
    mEnvironmentSegmentContexts.clear();
    mEnvironmentDrawRecords.clear();

    for (ShadowRenderContext& ShadowRenderContextValue : mShadowRenderContexts) {
        ShadowRenderContextValue.mModelContexts.clear();
        ShadowRenderContextValue.mTerrainPatchContexts.clear();
        ShadowRenderContextValue.mDrawRecords.clear();
        ShadowRenderContextValue.mEnvironmentDrawRecords.clear();
    }
}

bool RenderGatherResult::Empty() const {
    return mModelContexts.empty() && mBoundingBoxContexts.empty() && mDebugGeometryContexts.empty() && mTerrainPatchContexts.empty() && mHasTerrainUploadFuture == false && mDrawRecords.empty() && mBonePalette.empty() && mEnvironmentInstanceContexts.empty() && mEnvironmentSegmentContexts.empty() && mEnvironmentDrawRecords.empty() && AreShadowRenderContextsEmpty(mShadowRenderContexts);
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

    if (mHasTerrainUploadFuture == false && Other.mHasTerrainUploadFuture) {
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

    if (mHasTerrainUploadFuture == false && Other.mHasTerrainUploadFuture) {
        mTerrainUploadFuture = std::move(Other.mTerrainUploadFuture);
        mHasTerrainUploadFuture = true;
    }

    AppendDrawRecords(std::move(Other.mDrawRecords), ModelContextOffset, TerrainPatchContextOffset, mDrawRecords);
    mBonePalette.insert(mBonePalette.end(), std::make_move_iterator(Other.mBonePalette.begin()), std::make_move_iterator(Other.mBonePalette.end()));
    mEnvironmentInstanceContexts.insert(mEnvironmentInstanceContexts.end(), std::make_move_iterator(Other.mEnvironmentInstanceContexts.begin()), std::make_move_iterator(Other.mEnvironmentInstanceContexts.end()));
    mEnvironmentSegmentContexts.insert(mEnvironmentSegmentContexts.end(), std::make_move_iterator(Other.mEnvironmentSegmentContexts.begin()), std::make_move_iterator(Other.mEnvironmentSegmentContexts.end()));
    AppendEnvironmentDrawRecords(std::move(Other.mEnvironmentDrawRecords), EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset, mEnvironmentDrawRecords);
    AppendShadowRenderContexts(std::move(Other.mShadowRenderContexts), BonePaletteOffset, EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset);
    Other.Clear();
}

std::vector<ModelContext>& RenderGatherResult::GetModelContexts() {
    return mModelContexts;
}

const std::vector<ModelContext>& RenderGatherResult::GetModelContexts() const {
    return mModelContexts;
}

std::vector<BoundingBoxContext>& RenderGatherResult::GetBoundingBoxContexts() {
    return mBoundingBoxContexts;
}

const std::vector<BoundingBoxContext>& RenderGatherResult::GetBoundingBoxContexts() const {
    return mBoundingBoxContexts;
}

std::vector<DebugGeometryContext>& RenderGatherResult::GetDebugGeometryContexts() {
    return mDebugGeometryContexts;
}

const std::vector<DebugGeometryContext>& RenderGatherResult::GetDebugGeometryContexts() const {
    return mDebugGeometryContexts;
}

std::vector<TerrainPatchContext>& RenderGatherResult::GetTerrainPatchContexts() {
    return mTerrainPatchContexts;
}

const std::vector<TerrainPatchContext>& RenderGatherResult::GetTerrainPatchContexts() const {
    return mTerrainPatchContexts;
}

bool RenderGatherResult::HasTerrainUploadFuture() const {
    return mHasTerrainUploadFuture;
}

void RenderGatherResult::SetTerrainUploadFuture(const Future& TerrainUploadFuture) {
    mTerrainUploadFuture = TerrainUploadFuture;
    mHasTerrainUploadFuture = TerrainUploadFuture.IsValid();
}

const Future& RenderGatherResult::GetTerrainUploadFuture() const {
    return mTerrainUploadFuture;
}

std::vector<DrawRecord>& RenderGatherResult::GetDrawRecords() {
    return mDrawRecords;
}

const std::vector<DrawRecord>& RenderGatherResult::GetDrawRecords() const {
    return mDrawRecords;
}

std::vector<DirectX::SimpleMath::Matrix>& RenderGatherResult::GetBonePalette() {
    return mBonePalette;
}

const std::vector<DirectX::SimpleMath::Matrix>& RenderGatherResult::GetBonePalette() const {
    return mBonePalette;
}

std::vector<EnvironmentInstanceContext>& RenderGatherResult::GetEnvironmentInstanceContexts() {
    return mEnvironmentInstanceContexts;
}

const std::vector<EnvironmentInstanceContext>& RenderGatherResult::GetEnvironmentInstanceContexts() const {
    return mEnvironmentInstanceContexts;
}

std::vector<EnvironmentSegmentContext>& RenderGatherResult::GetEnvironmentSegmentContexts() {
    return mEnvironmentSegmentContexts;
}

const std::vector<EnvironmentSegmentContext>& RenderGatherResult::GetEnvironmentSegmentContexts() const {
    return mEnvironmentSegmentContexts;
}

std::vector<EnvironmentDrawRecord>& RenderGatherResult::GetEnvironmentDrawRecords() {
    return mEnvironmentDrawRecords;
}

const std::vector<EnvironmentDrawRecord>& RenderGatherResult::GetEnvironmentDrawRecords() const {
    return mEnvironmentDrawRecords;
}

std::array<ShadowRenderContext, ShadowCascadeMaxCount>& RenderGatherResult::GetShadowRenderContexts() {
    return mShadowRenderContexts;
}

const std::array<ShadowRenderContext, ShadowCascadeMaxCount>& RenderGatherResult::GetShadowRenderContexts() const {
    return mShadowRenderContexts;
}

std::uint32_t RenderGatherResult::AddIndexOffset(std::uint32_t Index, std::size_t Offset) {
    return Index + static_cast<std::uint32_t>(Offset);
}

ModelContext RenderGatherResult::BuildAdjustedModelContext(ModelContext ModelContextValue, std::size_t ModelContextOffset, std::size_t BonePaletteOffset) {
    ModelContextValue.mObjectId = AddIndexOffset(ModelContextValue.mObjectId, ModelContextOffset);
    if ((ModelContextValue.mFlags & SkinnedModelContextFlagBitMask) != 0u) {
        ModelContextValue.mBoneIndexStart = AddIndexOffset(ModelContextValue.mBoneIndexStart, BonePaletteOffset);
    }

    return ModelContextValue;
}

DrawRecord RenderGatherResult::BuildAdjustedDrawRecord(DrawRecord DrawRecordValue, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset) {
    DrawRecordValue.mObjectIndex = AddIndexOffset(DrawRecordValue.mObjectIndex, ModelContextOffset);
    if (DrawRecordValue.mTerrainPatchContextIndex != InvalidRenderIndex) {
        DrawRecordValue.mTerrainPatchContextIndex = AddIndexOffset(DrawRecordValue.mTerrainPatchContextIndex, TerrainPatchContextOffset);
    }

    return DrawRecordValue;
}

EnvironmentDrawRecord RenderGatherResult::BuildAdjustedEnvironmentDrawRecord(EnvironmentDrawRecord DrawRecordValue, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset) {
    DrawRecordValue.mInstanceOffset = AddIndexOffset(DrawRecordValue.mInstanceOffset, EnvironmentInstanceContextOffset);
    DrawRecordValue.mSegmentContextIndex = AddIndexOffset(DrawRecordValue.mSegmentContextIndex, EnvironmentSegmentContextOffset);
    return DrawRecordValue;
}

bool RenderGatherResult::AreShadowRenderContextsEmpty(const std::array<ShadowRenderContext, ShadowCascadeMaxCount>& ShadowRenderContexts) {
    for (const ShadowRenderContext& ShadowRenderContextValue : ShadowRenderContexts) {
        if (ShadowRenderContextValue.mModelContexts.empty() == false || ShadowRenderContextValue.mTerrainPatchContexts.empty() == false || ShadowRenderContextValue.mDrawRecords.empty() == false || ShadowRenderContextValue.mEnvironmentDrawRecords.empty() == false) {
            return false;
        }
    }

    return true;
}

void RenderGatherResult::AppendModelContexts(const std::vector<ModelContext>& SourceModelContexts, std::size_t ModelContextOffset, std::size_t BonePaletteOffset, std::vector<ModelContext>& OutModelContexts) {
    OutModelContexts.reserve(OutModelContexts.size() + SourceModelContexts.size());
    for (const ModelContext& SourceModelContext : SourceModelContexts) {
        OutModelContexts.push_back(BuildAdjustedModelContext(SourceModelContext, ModelContextOffset, BonePaletteOffset));
    }
}

void RenderGatherResult::AppendModelContexts(std::vector<ModelContext>&& SourceModelContexts, std::size_t ModelContextOffset, std::size_t BonePaletteOffset, std::vector<ModelContext>& OutModelContexts) {
    OutModelContexts.reserve(OutModelContexts.size() + SourceModelContexts.size());
    for (ModelContext& SourceModelContext : SourceModelContexts) {
        OutModelContexts.push_back(BuildAdjustedModelContext(std::move(SourceModelContext), ModelContextOffset, BonePaletteOffset));
    }
}

void RenderGatherResult::AppendDrawRecords(const std::vector<DrawRecord>& SourceDrawRecords, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset, std::vector<DrawRecord>& OutDrawRecords) {
    OutDrawRecords.reserve(OutDrawRecords.size() + SourceDrawRecords.size());
    for (const DrawRecord& SourceDrawRecord : SourceDrawRecords) {
        OutDrawRecords.push_back(BuildAdjustedDrawRecord(SourceDrawRecord, ModelContextOffset, TerrainPatchContextOffset));
    }
}

void RenderGatherResult::AppendDrawRecords(std::vector<DrawRecord>&& SourceDrawRecords, std::size_t ModelContextOffset, std::size_t TerrainPatchContextOffset, std::vector<DrawRecord>& OutDrawRecords) {
    OutDrawRecords.reserve(OutDrawRecords.size() + SourceDrawRecords.size());
    for (DrawRecord& SourceDrawRecord : SourceDrawRecords) {
        OutDrawRecords.push_back(BuildAdjustedDrawRecord(std::move(SourceDrawRecord), ModelContextOffset, TerrainPatchContextOffset));
    }
}

void RenderGatherResult::AppendEnvironmentDrawRecords(const std::vector<EnvironmentDrawRecord>& SourceDrawRecords, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset, std::vector<EnvironmentDrawRecord>& OutDrawRecords) {
    OutDrawRecords.reserve(OutDrawRecords.size() + SourceDrawRecords.size());
    for (const EnvironmentDrawRecord& SourceDrawRecord : SourceDrawRecords) {
        OutDrawRecords.push_back(BuildAdjustedEnvironmentDrawRecord(SourceDrawRecord, EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset));
    }
}

void RenderGatherResult::AppendEnvironmentDrawRecords(std::vector<EnvironmentDrawRecord>&& SourceDrawRecords, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset, std::vector<EnvironmentDrawRecord>& OutDrawRecords) {
    OutDrawRecords.reserve(OutDrawRecords.size() + SourceDrawRecords.size());
    for (EnvironmentDrawRecord& SourceDrawRecord : SourceDrawRecords) {
        OutDrawRecords.push_back(BuildAdjustedEnvironmentDrawRecord(std::move(SourceDrawRecord), EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset));
    }
}

void RenderGatherResult::AppendShadowRenderContexts(const std::array<ShadowRenderContext, ShadowCascadeMaxCount>& OtherShadowRenderContexts, std::size_t BonePaletteOffset, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset) {
    for (std::size_t ShadowContextIndex{}; ShadowContextIndex < mShadowRenderContexts.size(); ShadowContextIndex += 1ULL) {
        ShadowRenderContext& TargetShadowRenderContext{ mShadowRenderContexts[ShadowContextIndex] };
        const ShadowRenderContext& SourceShadowRenderContext{ OtherShadowRenderContexts[ShadowContextIndex] };
        const std::size_t ModelContextOffset{ TargetShadowRenderContext.mModelContexts.size() };
        const std::size_t TerrainPatchContextOffset{ TargetShadowRenderContext.mTerrainPatchContexts.size() };

        AppendModelContexts(SourceShadowRenderContext.mModelContexts, ModelContextOffset, BonePaletteOffset, TargetShadowRenderContext.mModelContexts);
        TargetShadowRenderContext.mTerrainPatchContexts.insert(TargetShadowRenderContext.mTerrainPatchContexts.end(), SourceShadowRenderContext.mTerrainPatchContexts.begin(), SourceShadowRenderContext.mTerrainPatchContexts.end());
        AppendDrawRecords(SourceShadowRenderContext.mDrawRecords, ModelContextOffset, TerrainPatchContextOffset, TargetShadowRenderContext.mDrawRecords);
        AppendEnvironmentDrawRecords(SourceShadowRenderContext.mEnvironmentDrawRecords, EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset, TargetShadowRenderContext.mEnvironmentDrawRecords);
    }
}

void RenderGatherResult::AppendShadowRenderContexts(std::array<ShadowRenderContext, ShadowCascadeMaxCount>&& OtherShadowRenderContexts, std::size_t BonePaletteOffset, std::size_t EnvironmentInstanceContextOffset, std::size_t EnvironmentSegmentContextOffset) {
    for (std::size_t ShadowContextIndex{}; ShadowContextIndex < mShadowRenderContexts.size(); ShadowContextIndex += 1ULL) {
        ShadowRenderContext& TargetShadowRenderContext{ mShadowRenderContexts[ShadowContextIndex] };
        ShadowRenderContext& SourceShadowRenderContext{ OtherShadowRenderContexts[ShadowContextIndex] };
        const std::size_t ModelContextOffset{ TargetShadowRenderContext.mModelContexts.size() };
        const std::size_t TerrainPatchContextOffset{ TargetShadowRenderContext.mTerrainPatchContexts.size() };

        AppendModelContexts(std::move(SourceShadowRenderContext.mModelContexts), ModelContextOffset, BonePaletteOffset, TargetShadowRenderContext.mModelContexts);
        TargetShadowRenderContext.mTerrainPatchContexts.insert(TargetShadowRenderContext.mTerrainPatchContexts.end(), std::make_move_iterator(SourceShadowRenderContext.mTerrainPatchContexts.begin()), std::make_move_iterator(SourceShadowRenderContext.mTerrainPatchContexts.end()));
        AppendDrawRecords(std::move(SourceShadowRenderContext.mDrawRecords), ModelContextOffset, TerrainPatchContextOffset, TargetShadowRenderContext.mDrawRecords);
        AppendEnvironmentDrawRecords(std::move(SourceShadowRenderContext.mEnvironmentDrawRecords), EnvironmentInstanceContextOffset, EnvironmentSegmentContextOffset, TargetShadowRenderContext.mEnvironmentDrawRecords);
    }
}
