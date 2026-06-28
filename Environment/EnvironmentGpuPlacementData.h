#pragma once

#include <cstdint>
#include <vector>

#include <DirectXTK12/SimpleMath.h>

namespace Game {
    struct alignas(16) EnvironmentGpuPlacementConfig final {
    public:
        DirectX::SimpleMath::Vector4 mFocusPositionRenderRadius{};
        DirectX::SimpleMath::Vector4 mTerrainPosition{};
        DirectX::SimpleMath::Vector4 mTerrainScale{};
        DirectX::SimpleMath::Vector4 mTerrainGridParameters{};
        DirectX::SimpleMath::Vector4 mTerrainSizeParameters{};
        DirectX::SimpleMath::Vector4 mDensityParameters{};
        DirectX::SimpleMath::Vector4 mClusterParameters{};
        DirectX::SimpleMath::Vector4 mClumpParameters0{};
        DirectX::SimpleMath::Vector4 mClumpParameters1{};
        DirectX::SimpleMath::Vector4 mForestParameters0{};
        DirectX::SimpleMath::Vector4 mForestParameters1{};
        DirectX::SimpleMath::Vector4 mForestParameters2{};
        std::uint32_t mTerrainSeed{};
        std::uint32_t mCandidateRandomXStream{};
        std::uint32_t mCandidateRandomZStream{};
        std::uint32_t mCandidateRandomChanceStream{};
        std::uint32_t mCandidateRandomYawStream{};
        std::uint32_t mCandidateRandomScaleStream{};
        std::uint32_t mClusterCornerStream{};
        std::uint32_t mClumpCenterXStream{};
        std::uint32_t mClumpCenterZStream{};
        std::uint32_t mClumpAngleStream{};
        std::uint32_t mClumpDistanceStream{};
        std::uint32_t mForestPatchCornerStream{};
        std::uint32_t mForestPatchNoiseIndex{};
        std::uint32_t mMinimumSpacingPriorityStream{};
        std::uint32_t mSeedSalt{};
        std::uint32_t mMinimumSpacingCellRadius{};
    };

    struct alignas(16) EnvironmentGpuPlacementRule final {
    public:
        DirectX::SimpleMath::Vector4 mScaleYawOffset{};
        DirectX::SimpleMath::Vector4 mDensityCluster{};
        DirectX::SimpleMath::Vector4 mClusterShape{};
        DirectX::SimpleMath::Vector4 mClusterForest{};
        std::uint32_t mLayerIndex{};
        std::uint32_t mExcludedLayerMask{};
        std::uint32_t mInstancesPerCell{};
        std::uint32_t mPadding0{};
    };

    struct alignas(16) EnvironmentGpuPlacementDrawRecord final {
    public:
        std::int32_t mMinimumCellX{};
        std::int32_t mMinimumCellZ{};
        std::uint32_t mCellCountX{};
        std::uint32_t mCellCountZ{};
        std::uint32_t mRuleIndex{};
        std::uint32_t mLodIndex{};
        float mMinimumDistance{};
        float mMaximumDistance{};
        std::uint32_t mCandidateOffset{};
        std::uint32_t mCandidateCount{};
        std::uint32_t mCullingCenterYOffsetBits{};
        std::uint32_t mCullingRadiusBits{};
    };

    struct alignas(16) EnvironmentGpuPlacementCandidateRecord final {
    public:
        std::int32_t mMinimumCellX{};
        std::int32_t mMinimumCellZ{};
        std::uint32_t mCellCountX{};
        std::uint32_t mCellCountZ{};
        std::uint32_t mRuleIndex{};
        std::uint32_t mCandidateOffset{};
        std::uint32_t mCandidateCount{};
        std::uint32_t mCellMetadataOffset{};
    };

    struct alignas(16) EnvironmentGpuPlacementCandidateDispatchRecord final {
    public:
        std::uint32_t mCandidateRecordIndex{};
        std::int32_t mCellX{};
        std::int32_t mCellZ{};
        std::uint32_t mInstanceOffset{};
    };

    struct alignas(16) EnvironmentGpuPlacementDrawDispatchRecord final {
    public:
        std::uint32_t mDrawRecordIndex{};
        std::int32_t mCellX{};
        std::int32_t mCellZ{};
        std::uint32_t mInstanceOffset{};
    };

    struct alignas(16) EnvironmentGpuPlacementSpacingRuleRecord final {
    public:
        std::uint32_t mRuleIndex{};
        std::uint32_t mPadding0{};
        std::uint32_t mPadding1{};
        std::uint32_t mPadding2{};
    };

    struct alignas(16) EnvironmentGpuPlacementCandidate final {
    public:
        DirectX::SimpleMath::Vector4 mPositionScale{};
        DirectX::SimpleMath::Vector4 mRotationValid{};
        std::int32_t mCellX{};
        std::int32_t mCellZ{};
        std::uint32_t mRuleIndex{};
        std::uint32_t mInstanceIndex{};
    };

    struct alignas(16) EnvironmentGpuPlacementCellMetadata final {
    public:
        std::int32_t mCellX{};
        std::int32_t mCellZ{};
        std::uint32_t mRuleIndex{};
        std::uint32_t mState{};
        std::uint32_t mCandidateOffset{};
        std::uint32_t mCandidateCount{};
        std::uint32_t mAcceptedCandidateOffset{};
        std::uint32_t mAcceptedCandidateCount{};
        std::uint32_t mLastTouchedFrameLow{};
        std::uint32_t mLastTouchedFrameHigh{};
        std::uint32_t mPadding0{};
        std::uint32_t mPadding1{};
    };

    static_assert(sizeof(EnvironmentGpuPlacementConfig) == 256u);
    static_assert(sizeof(EnvironmentGpuPlacementRule) == 80u);
    static_assert(sizeof(EnvironmentGpuPlacementDrawRecord) == 48u);
    static_assert(sizeof(EnvironmentGpuPlacementCandidateRecord) == 32u);
    static_assert(sizeof(EnvironmentGpuPlacementCandidateDispatchRecord) == 16u);
    static_assert(sizeof(EnvironmentGpuPlacementDrawDispatchRecord) == 16u);
    static_assert(sizeof(EnvironmentGpuPlacementSpacingRuleRecord) == 16u);
    static_assert(sizeof(EnvironmentGpuPlacementCandidate) == 48u);
    static_assert(sizeof(EnvironmentGpuPlacementCellMetadata) == 48u);

    struct EnvironmentGpuPlacementFrameData final {
    public:
        EnvironmentGpuPlacementConfig mConfig{};
        std::vector<EnvironmentGpuPlacementRule> mRules{};
        std::vector<EnvironmentGpuPlacementCandidateRecord> mCandidateRecords{};
        std::vector<EnvironmentGpuPlacementCandidateDispatchRecord> mCandidateDispatchRecords{};
        std::vector<EnvironmentGpuPlacementDrawDispatchRecord> mDrawDispatchRecords{};
        std::vector<EnvironmentGpuPlacementSpacingRuleRecord> mSpacingRuleRecords{};
        std::vector<EnvironmentGpuPlacementDrawRecord> mDrawRecords{};
        std::uint32_t mCandidateCount{};
    };
}
