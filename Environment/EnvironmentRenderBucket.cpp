#include "EnvironmentRenderBucket.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <tuple>
#include <utility>

#include "RenderContract/Shadow/ShadowRenderContext.h"

namespace Game {
    namespace {
        constexpr std::uint32_t EnvironmentRenderMainVisibilityMaskBit{ 1u };

        void AppendHashValue(std::uint64_t& InOutHash, std::uint64_t Value) {
            InOutHash ^= Value;
            InOutHash *= 1099511628211ULL;
        }

        std::array<std::uint32_t, 16ULL> BuildMatrixHashBits(const DirectX::SimpleMath::Matrix& MatrixValue) {
            return std::array<std::uint32_t, 16ULL>{ std::bit_cast<std::uint32_t>(MatrixValue._11), std::bit_cast<std::uint32_t>(MatrixValue._12), std::bit_cast<std::uint32_t>(MatrixValue._13), std::bit_cast<std::uint32_t>(MatrixValue._14), std::bit_cast<std::uint32_t>(MatrixValue._21), std::bit_cast<std::uint32_t>(MatrixValue._22), std::bit_cast<std::uint32_t>(MatrixValue._23), std::bit_cast<std::uint32_t>(MatrixValue._24), std::bit_cast<std::uint32_t>(MatrixValue._31), std::bit_cast<std::uint32_t>(MatrixValue._32), std::bit_cast<std::uint32_t>(MatrixValue._33), std::bit_cast<std::uint32_t>(MatrixValue._34), std::bit_cast<std::uint32_t>(MatrixValue._41), std::bit_cast<std::uint32_t>(MatrixValue._42), std::bit_cast<std::uint32_t>(MatrixValue._43), std::bit_cast<std::uint32_t>(MatrixValue._44) };
        }

        EnvironmentDrawBucketKey BuildEnvironmentDrawBucketKey(const RenderContract::EnvironmentDrawRecord& DrawRecord, const RenderContract::EnvironmentSegmentContext& SegmentContext, std::uint32_t PrototypeIndex, std::uint32_t LodIndex) {
            EnvironmentDrawBucketKey Key{};
            Key.mPipeline = DrawRecord.mPipeline;
            Key.mMesh = DrawRecord.mMesh;
            Key.mLocalTransformBits = BuildMatrixHashBits(SegmentContext.mLocalTransform);
            Key.mSubMesh = DrawRecord.mSubMesh;
            Key.mPass = DrawRecord.mPass;
            Key.mMaterialIndex = DrawRecord.mMaterialIndex;
            Key.mPrototypeIndex = PrototypeIndex;
            Key.mLodIndex = LodIndex;
            Key.mFlags = DrawRecord.mFlags;
            Key.mShadowCascadeMask = DrawRecord.mShadowCascadeMask;
            Key.mCastsShadow = DrawRecord.mCastsShadow;
            return Key;
        }

        bool CompareEnvironmentDrawBucketByKey(const EnvironmentDrawBucket& Left, const EnvironmentDrawBucket& Right) {
            return Left.mKey < Right.mKey;
        }

        std::uint32_t ClampToUint32(std::size_t Value) {
            return static_cast<std::uint32_t>(std::min<std::size_t>(Value, std::numeric_limits<std::uint32_t>::max()));
        }

        bool HasVisibleRun(const EnvironmentDrawBucket& Bucket) {
            for (const EnvironmentDrawBucketInstanceRun& Run : Bucket.mRuns) {
                if (Run.mInstanceContexts.empty() == false) {
                    return true;
                }
            }

            return false;
        }
    }

    bool operator==(const EnvironmentDrawBucketKey& Left, const EnvironmentDrawBucketKey& Right) {
        return Left.mPipeline == Right.mPipeline && Left.mMesh == Right.mMesh && Left.mLocalTransformBits == Right.mLocalTransformBits && Left.mSubMesh == Right.mSubMesh && Left.mPass == Right.mPass && Left.mMaterialIndex == Right.mMaterialIndex && Left.mPrototypeIndex == Right.mPrototypeIndex && Left.mLodIndex == Right.mLodIndex && Left.mFlags == Right.mFlags && Left.mShadowCascadeMask == Right.mShadowCascadeMask && Left.mCastsShadow == Right.mCastsShadow;
    }

    bool operator!=(const EnvironmentDrawBucketKey& Left, const EnvironmentDrawBucketKey& Right) {
        return (Left == Right) == false;
    }

    bool operator<(const EnvironmentDrawBucketKey& Left, const EnvironmentDrawBucketKey& Right) {
        return std::tie(Left.mPass, Left.mPipeline, Left.mMesh, Left.mSubMesh, Left.mMaterialIndex, Left.mPrototypeIndex, Left.mLodIndex, Left.mFlags, Left.mShadowCascadeMask, Left.mCastsShadow, Left.mLocalTransformBits) < std::tie(Right.mPass, Right.mPipeline, Right.mMesh, Right.mSubMesh, Right.mMaterialIndex, Right.mPrototypeIndex, Right.mLodIndex, Right.mFlags, Right.mShadowCascadeMask, Right.mCastsShadow, Right.mLocalTransformBits);
    }

    std::size_t EnvironmentDrawBucketKeyHasher::operator()(const EnvironmentDrawBucketKey& Key) const {
        std::uint64_t Hash{ 1469598103934665603ULL };
        AppendHashValue(Hash, reinterpret_cast<std::uintptr_t>(Key.mPipeline));
        AppendHashValue(Hash, reinterpret_cast<std::uintptr_t>(Key.mMesh));
        AppendHashValue(Hash, Key.mSubMesh);
        AppendHashValue(Hash, Key.mPass);
        AppendHashValue(Hash, Key.mMaterialIndex);
        AppendHashValue(Hash, Key.mPrototypeIndex);
        AppendHashValue(Hash, Key.mLodIndex);
        AppendHashValue(Hash, Key.mFlags);
        AppendHashValue(Hash, Key.mShadowCascadeMask);
        AppendHashValue(Hash, Key.mCastsShadow == true ? 1u : 0u);
        for (std::uint32_t MatrixValue : Key.mLocalTransformBits) {
            AppendHashValue(Hash, MatrixValue);
        }

        return static_cast<std::size_t>(Hash);
    }

    EnvironmentDrawBucketBuilder::EnvironmentDrawBucketBuilder()
        : mBuckets{},
        mBucketIndexByKey{},
        mExpectedInstanceCount{} {
    }

    EnvironmentDrawBucketBuilder::~EnvironmentDrawBucketBuilder() {
    }

    EnvironmentDrawBucketBuilder::EnvironmentDrawBucketBuilder(const EnvironmentDrawBucketBuilder& Other)
        : mBuckets{ Other.mBuckets },
        mBucketIndexByKey{ Other.mBucketIndexByKey },
        mExpectedInstanceCount{ Other.mExpectedInstanceCount } {
    }

    EnvironmentDrawBucketBuilder& EnvironmentDrawBucketBuilder::operator=(const EnvironmentDrawBucketBuilder& Other) {
        if (this == &Other) {
            return *this;
        }

        mBuckets = Other.mBuckets;
        mBucketIndexByKey = Other.mBucketIndexByKey;
        mExpectedInstanceCount = Other.mExpectedInstanceCount;
        return *this;
    }

    EnvironmentDrawBucketBuilder::EnvironmentDrawBucketBuilder(EnvironmentDrawBucketBuilder&& Other) noexcept
        : mBuckets{ std::move(Other.mBuckets) },
        mBucketIndexByKey{ std::move(Other.mBucketIndexByKey) },
        mExpectedInstanceCount{ Other.mExpectedInstanceCount } {
        Other.mExpectedInstanceCount = 0ULL;
    }

    EnvironmentDrawBucketBuilder& EnvironmentDrawBucketBuilder::operator=(EnvironmentDrawBucketBuilder&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mBuckets = std::move(Other.mBuckets);
        mBucketIndexByKey = std::move(Other.mBucketIndexByKey);
        mExpectedInstanceCount = Other.mExpectedInstanceCount;
        Other.mExpectedInstanceCount = 0ULL;
        return *this;
    }

    void EnvironmentDrawBucketBuilder::Clear() {
        mBuckets.clear();
        mBucketIndexByKey.clear();
        mExpectedInstanceCount = 0ULL;
    }

    void EnvironmentDrawBucketBuilder::Reserve(std::size_t BucketCount, std::size_t InstanceCount) {
        mBuckets.reserve(BucketCount);
        mBucketIndexByKey.reserve(BucketCount);
        mExpectedInstanceCount = InstanceCount;
    }

    bool EnvironmentDrawBucketBuilder::Empty() const {
        return mBuckets.empty();
    }

    void EnvironmentDrawBucketBuilder::AppendInstances(const RenderContract::EnvironmentDrawRecord& DrawRecord, const RenderContract::EnvironmentSegmentContext& SegmentContext, std::span<const RenderContract::EnvironmentInstanceContext> InstanceContexts, std::uint32_t PrototypeIndex, std::uint32_t LodIndex, std::uint32_t VisibilityMask) {
        if (VisibilityMask == 0u || InstanceContexts.empty() == true || DrawRecord.mMesh == nullptr || DrawRecord.mInstanceCount == 0u) {
            return;
        }

        EnvironmentDrawBucket& Bucket{ ResolveBucket(DrawRecord, SegmentContext, PrototypeIndex, LodIndex) };
        EnvironmentDrawBucketInstanceRun& Run{ ResolveRun(Bucket, VisibilityMask) };
        if (mExpectedInstanceCount > 0ULL) {
            Run.mInstanceContexts.reserve(std::min<std::size_t>(mExpectedInstanceCount, Run.mInstanceContexts.size() + InstanceContexts.size()));
        }

        EnvironmentCellRange CellRange{};
        CellRange.mInstanceOffset = ClampToUint32(Run.mInstanceContexts.size());
        CellRange.mInstanceCount = ClampToUint32(InstanceContexts.size());
        Run.mCellRanges.push_back(CellRange);
        Run.mInstanceContexts.insert(Run.mInstanceContexts.end(), InstanceContexts.begin(), InstanceContexts.end());
    }

    void EnvironmentDrawBucketBuilder::AppendToRenderGatherResult(RenderContract::RenderGatherResult& OutRenderGatherResult) const {
        if (mBuckets.empty() == true) {
            return;
        }

        std::vector<const EnvironmentDrawBucket*> OrderedBuckets{};
        OrderedBuckets.reserve(mBuckets.size());
        for (const EnvironmentDrawBucket& Bucket : mBuckets) {
            if (HasVisibleRun(Bucket) == true) {
                OrderedBuckets.push_back(&Bucket);
            }
        }

        std::stable_sort(OrderedBuckets.begin(), OrderedBuckets.end(), [](const EnvironmentDrawBucket* Left, const EnvironmentDrawBucket* Right) {
            return CompareEnvironmentDrawBucketByKey(*Left, *Right);
        });

        for (const EnvironmentDrawBucket* BucketPointer : OrderedBuckets) {
            const EnvironmentDrawBucket& Bucket{ *BucketPointer };
            const std::uint32_t SegmentContextIndex{ ClampToUint32(OutRenderGatherResult.GetEnvironmentSegmentContexts().size()) };
            OutRenderGatherResult.GetEnvironmentSegmentContexts().push_back(Bucket.mSegmentContext);
            for (const EnvironmentDrawBucketInstanceRun& Run : Bucket.mRuns) {
                if (Run.mInstanceContexts.empty() == true) {
                    continue;
                }

                const std::uint32_t InstanceOffset{ ClampToUint32(OutRenderGatherResult.GetEnvironmentInstanceContexts().size()) };
                OutRenderGatherResult.GetEnvironmentInstanceContexts().insert(OutRenderGatherResult.GetEnvironmentInstanceContexts().end(), Run.mInstanceContexts.begin(), Run.mInstanceContexts.end());

                RenderContract::EnvironmentDrawRecord DrawRecord{ Bucket.mDrawRecord };
                DrawRecord.mInstanceOffset = InstanceOffset;
                DrawRecord.mInstanceCount = ClampToUint32(Run.mInstanceContexts.size());
                DrawRecord.mSegmentContextIndex = SegmentContextIndex;

                if ((Run.mVisibilityMask & EnvironmentRenderMainVisibilityMaskBit) != 0u) {
                    OutRenderGatherResult.GetEnvironmentDrawRecords().push_back(DrawRecord);
                }

                if (DrawRecord.mCastsShadow == false) {
                    continue;
                }

                std::array<RenderContract::ShadowRenderContext, RenderContract::ShadowCascadeMaxCount>& ShadowRenderContexts{ OutRenderGatherResult.GetShadowRenderContexts() };
                for (std::uint32_t CascadeIndex{}; CascadeIndex < ShadowRenderContexts.size(); CascadeIndex += 1u) {
                    const std::uint32_t CascadeBit{ 1u << CascadeIndex };
                    if ((Run.mVisibilityMask & BuildEnvironmentRenderShadowVisibilityMaskBit(CascadeIndex)) != 0u && (DrawRecord.mShadowCascadeMask & CascadeBit) != 0u) {
                        ShadowRenderContexts[CascadeIndex].mEnvironmentDrawRecords.push_back(DrawRecord);
                    }
                }
            }
        }
    }

    EnvironmentDrawBucket& EnvironmentDrawBucketBuilder::ResolveBucket(const RenderContract::EnvironmentDrawRecord& DrawRecord, const RenderContract::EnvironmentSegmentContext& SegmentContext, std::uint32_t PrototypeIndex, std::uint32_t LodIndex) {
        const EnvironmentDrawBucketKey Key{ BuildEnvironmentDrawBucketKey(DrawRecord, SegmentContext, PrototypeIndex, LodIndex) };
        const std::unordered_map<EnvironmentDrawBucketKey, std::size_t, EnvironmentDrawBucketKeyHasher>::const_iterator FoundIterator{ mBucketIndexByKey.find(Key) };
        if (FoundIterator != mBucketIndexByKey.end()) {
            return mBuckets[FoundIterator->second];
        }

        EnvironmentDrawBucket Bucket{};
        Bucket.mKey = Key;
        Bucket.mSegmentContext = SegmentContext;
        Bucket.mDrawRecord = DrawRecord;
        const std::size_t BucketIndex{ mBuckets.size() };
        mBuckets.push_back(std::move(Bucket));
        mBucketIndexByKey.insert_or_assign(Key, BucketIndex);
        return mBuckets.back();
    }

    EnvironmentDrawBucketInstanceRun& EnvironmentDrawBucketBuilder::ResolveRun(EnvironmentDrawBucket& Bucket, std::uint32_t VisibilityMask) {
        for (EnvironmentDrawBucketInstanceRun& Run : Bucket.mRuns) {
            if (Run.mVisibilityMask == VisibilityMask) {
                return Run;
            }
        }

        EnvironmentDrawBucketInstanceRun Run{};
        Run.mVisibilityMask = VisibilityMask;
        Bucket.mRuns.push_back(std::move(Run));
        return Bucket.mRuns.back();
    }

    std::uint32_t GetEnvironmentRenderMainVisibilityMaskBit() {
        return EnvironmentRenderMainVisibilityMaskBit;
    }

    std::uint32_t BuildEnvironmentRenderShadowVisibilityMaskBit(std::uint32_t CascadeIndex) {
        return 1u << (CascadeIndex + 1u);
    }

    std::uint32_t BuildEnvironmentRenderVisibilityMask(bool IsMainVisible, std::uint32_t ShadowCascadeMask) {
        std::uint32_t VisibilityMask{};
        if (IsMainVisible == true) {
            VisibilityMask |= EnvironmentRenderMainVisibilityMaskBit;
        }

        for (std::uint32_t CascadeIndex{}; CascadeIndex < RenderContract::ShadowCascadeMaxCount; CascadeIndex += 1u) {
            if ((ShadowCascadeMask & (1u << CascadeIndex)) != 0u) {
                VisibilityMask |= BuildEnvironmentRenderShadowVisibilityMaskBit(CascadeIndex);
            }
        }

        return VisibilityMask;
    }
}
