#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "RenderContract/Environment/EnvironmentDrawRecord.h"
#include "RenderContract/Environment/EnvironmentInstanceContext.h"
#include "RenderContract/Environment/EnvironmentSegmentContext.h"
#include "RenderContract/Gather/RenderGatherResult.h"

namespace Game {
    struct EnvironmentCellRange final {
    public:
        std::uint32_t mInstanceOffset{};
        std::uint32_t mInstanceCount{};
    };

    struct EnvironmentDrawBucketKey final {
    public:
        const RenderContract::IPipeline* mPipeline{};
        const RenderContract::IModelNode* mMesh{};
        std::array<std::uint32_t, 16ULL> mLocalTransformBits{};
        std::uint32_t mSubMesh{};
        std::uint32_t mPass{};
        std::uint32_t mMaterialIndex{};
        std::uint32_t mPrototypeIndex{};
        std::uint32_t mLodIndex{};
        std::uint32_t mFlags{};
        std::uint32_t mShadowCascadeMask{ 0xffffffffu };
        bool mCastsShadow{ true };
    };

    bool operator==(const EnvironmentDrawBucketKey& Left, const EnvironmentDrawBucketKey& Right);
    bool operator!=(const EnvironmentDrawBucketKey& Left, const EnvironmentDrawBucketKey& Right);
    bool operator<(const EnvironmentDrawBucketKey& Left, const EnvironmentDrawBucketKey& Right);

    struct EnvironmentDrawBucketKeyHasher final {
    public:
        std::size_t operator()(const EnvironmentDrawBucketKey& Key) const;
    };

    struct EnvironmentDrawBucketInstanceRun final {
    public:
        std::vector<RenderContract::EnvironmentInstanceContext> mInstanceContexts{};
        std::vector<EnvironmentCellRange> mCellRanges{};
        std::uint32_t mVisibilityMask{};
    };

    struct EnvironmentDrawBucket final {
    public:
        EnvironmentDrawBucketKey mKey{};
        RenderContract::EnvironmentSegmentContext mSegmentContext{};
        RenderContract::EnvironmentDrawRecord mDrawRecord{};
        std::vector<EnvironmentDrawBucketInstanceRun> mRuns{};
    };

    class EnvironmentDrawBucketBuilder final {
    public:
        EnvironmentDrawBucketBuilder();
        ~EnvironmentDrawBucketBuilder();
        EnvironmentDrawBucketBuilder(const EnvironmentDrawBucketBuilder& Other);
        EnvironmentDrawBucketBuilder& operator=(const EnvironmentDrawBucketBuilder& Other);
        EnvironmentDrawBucketBuilder(EnvironmentDrawBucketBuilder&& Other) noexcept;
        EnvironmentDrawBucketBuilder& operator=(EnvironmentDrawBucketBuilder&& Other) noexcept;

    public:
        void Clear();
        void Reserve(std::size_t BucketCount, std::size_t InstanceCount);
        bool Empty() const;
        void AppendInstances(const RenderContract::EnvironmentDrawRecord& DrawRecord, const RenderContract::EnvironmentSegmentContext& SegmentContext, std::span<const RenderContract::EnvironmentInstanceContext> InstanceContexts, std::uint32_t PrototypeIndex, std::uint32_t LodIndex, std::uint32_t VisibilityMask);
        void AppendToRenderGatherResult(RenderContract::RenderGatherResult& OutRenderGatherResult) const;

    private:
        EnvironmentDrawBucket& ResolveBucket(const RenderContract::EnvironmentDrawRecord& DrawRecord, const RenderContract::EnvironmentSegmentContext& SegmentContext, std::uint32_t PrototypeIndex, std::uint32_t LodIndex);
        EnvironmentDrawBucketInstanceRun& ResolveRun(EnvironmentDrawBucket& Bucket, std::uint32_t VisibilityMask);

    private:
        std::vector<EnvironmentDrawBucket> mBuckets{};
        std::unordered_map<EnvironmentDrawBucketKey, std::size_t, EnvironmentDrawBucketKeyHasher> mBucketIndexByKey{};
        std::size_t mExpectedInstanceCount{};
    };

    std::uint32_t GetEnvironmentRenderMainVisibilityMaskBit();
    std::uint32_t BuildEnvironmentRenderShadowVisibilityMaskBit(std::uint32_t CascadeIndex);
    std::uint32_t BuildEnvironmentRenderVisibilityMask(bool IsMainVisible, std::uint32_t ShadowCascadeMask);
}
