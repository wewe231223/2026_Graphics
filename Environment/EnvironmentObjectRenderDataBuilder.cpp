#include "EnvironmentObjectRenderDataBuilder.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "Environment/EnvironmentRenderBucket.h"

namespace Game {
    namespace {
        RenderContract::EnvironmentInstanceContext BuildEnvironmentInstanceContext(const EnvironmentObjectInstance& Instance) {
            RenderContract::EnvironmentInstanceContext Context{};
            Context.mPositionScale = SimpleMath::Vector4{ Instance.mPosition.x, Instance.mPosition.y, Instance.mPosition.z, Instance.mScale };
            Context.mRotationVariation = SimpleMath::Vector4{ Instance.mYawRadians, static_cast<float>(Instance.mVariation), 0.0f, 0.0f };
            return Context;
        }

        RenderContract::EnvironmentSegmentContext BuildEnvironmentSegmentContext(const EnvironmentObjectBatch& Batch) {
            RenderContract::EnvironmentSegmentContext Context{};
            Context.mLocalTransform = Batch.mLocalTransform;
            return Context;
        }

        RenderContract::EnvironmentDrawRecord BuildEnvironmentDrawRecord(const EnvironmentObjectBatch& Batch, std::uint32_t SegmentContextIndex) {
            RenderContract::EnvironmentDrawRecord DrawRecord{};
            DrawRecord.mPipeline = Batch.mPipeline;
            DrawRecord.mMesh = Batch.mMesh;
            DrawRecord.mSubMesh = Batch.mSubMeshIndex;
            DrawRecord.mPass = 0u;
            DrawRecord.mInstanceOffset = Batch.mInstanceOffsetInCell;
            DrawRecord.mInstanceCount = Batch.mInstanceCount;
            DrawRecord.mSegmentContextIndex = SegmentContextIndex;
            DrawRecord.mMaterialIndex = Batch.mMaterialIndex;
            DrawRecord.mFlags = Batch.mFlags;
            DrawRecord.mShadowCascadeMask = Batch.mShadowCascadeMask;
            DrawRecord.mCastsShadow = Batch.mCastsShadow;
            return DrawRecord;
        }

        RenderContract::EnvironmentDrawRecord BuildOffsetEnvironmentDrawRecord(RenderContract::EnvironmentDrawRecord DrawRecord, std::size_t InstanceContextOffset, std::size_t SegmentContextOffset) {
            DrawRecord.mInstanceOffset += static_cast<std::uint32_t>(InstanceContextOffset);
            DrawRecord.mSegmentContextIndex += static_cast<std::uint32_t>(SegmentContextOffset);
            return DrawRecord;
        }

        bool IsRenderableBatch(const EnvironmentObjectBatch& Batch) {
            if (Batch.mPipeline == nullptr || Batch.mInstanceCount == 0u) {
                return false;
            }

            return Batch.mMesh != nullptr;
        }

        bool HasRenderableBatch(const std::vector<EnvironmentObjectBatch>& Batches) {
            for (const EnvironmentObjectBatch& Batch : Batches) {
                if (IsRenderableBatch(Batch) == true) {
                    return true;
                }
            }

            return false;
        }

        std::uint32_t ResolveEnvironmentObjectDrawRecordPrototypeIndex(const EnvironmentObjectRenderPacket& Packet, const RenderContract::EnvironmentDrawRecord& DrawRecord) {
            if (DrawRecord.mInstanceOffset >= Packet.mInstancePrototypeIndices.size()) {
                return InvalidEnvironmentObjectIndex;
            }

            return Packet.mInstancePrototypeIndices[DrawRecord.mInstanceOffset];
        }

        std::uint32_t ResolveEnvironmentObjectPrototypeLodLevel(const EnvironmentObjectRenderViewPacket& ViewPacket, std::uint32_t PrototypeIndex) {
            if (PrototypeIndex < ViewPacket.mPrototypeLodLevelCount && ViewPacket.mPrototypeLodLevels != nullptr) {
                return ViewPacket.mPrototypeLodLevels[PrototypeIndex];
            }

            return 0u;
        }

        std::span<const RenderContract::EnvironmentInstanceContext> BuildEnvironmentInstanceContextSpan(const EnvironmentObjectRenderPacket& Packet, const RenderContract::EnvironmentDrawRecord& DrawRecord) {
            const std::size_t InstanceBegin{ DrawRecord.mInstanceOffset };
            const std::size_t InstanceEnd{ InstanceBegin + DrawRecord.mInstanceCount };
            if (InstanceEnd > Packet.mInstanceContexts.size()) {
                return {};
            }

            return std::span<const RenderContract::EnvironmentInstanceContext>{ Packet.mInstanceContexts.data() + InstanceBegin, DrawRecord.mInstanceCount };
        }

        void AppendEnvironmentObjectViewPacketToBucketBuilder(const EnvironmentObjectRenderViewPacket& ViewPacket, EnvironmentDrawBucketBuilder& BucketBuilder) {
            if (ViewPacket.mPacket == nullptr || ViewPacket.mVisibilityMask == 0u || ViewPacket.mPacket->mLods.empty() == true) {
                return;
            }

            const EnvironmentObjectRenderPacket& Packet{ *ViewPacket.mPacket };
            for (std::size_t LodLevel{}; LodLevel < Packet.mLods.size(); LodLevel += 1ULL) {
                const EnvironmentObjectRenderPacketLod& Lod{ Packet.mLods[LodLevel] };
                for (const RenderContract::EnvironmentDrawRecord& DrawRecord : Lod.mDrawRecords) {
                    if (DrawRecord.mMesh == nullptr || DrawRecord.mInstanceCount == 0u || DrawRecord.mSegmentContextIndex >= Lod.mSegmentContexts.size()) {
                        continue;
                    }

                    const std::uint32_t PrototypeIndex{ ResolveEnvironmentObjectDrawRecordPrototypeIndex(Packet, DrawRecord) };
                    const std::uint32_t TargetLodLevel{ ResolveEnvironmentObjectPrototypeLodLevel(ViewPacket, PrototypeIndex) };
                    if (static_cast<std::size_t>(TargetLodLevel) != LodLevel) {
                        continue;
                    }

                    const std::span<const RenderContract::EnvironmentInstanceContext> InstanceContexts{ BuildEnvironmentInstanceContextSpan(Packet, DrawRecord) };
                    if (InstanceContexts.empty() == true) {
                        continue;
                    }

                    const RenderContract::EnvironmentSegmentContext& SegmentContext{ Lod.mSegmentContexts[DrawRecord.mSegmentContextIndex] };
                    BucketBuilder.AppendInstances(DrawRecord, SegmentContext, InstanceContexts, PrototypeIndex, static_cast<std::uint32_t>(LodLevel), ViewPacket.mVisibilityMask);
                }
            }
        }

        std::size_t CountEnvironmentObjectViewPacketDrawRecords(std::span<const EnvironmentObjectRenderViewPacket> ViewPackets) {
            std::size_t DrawRecordCount{};
            for (const EnvironmentObjectRenderViewPacket& ViewPacket : ViewPackets) {
                if (ViewPacket.mPacket == nullptr) {
                    continue;
                }

                for (const EnvironmentObjectRenderPacketLod& Lod : ViewPacket.mPacket->mLods) {
                    DrawRecordCount += Lod.mDrawRecords.size();
                }
            }

            return DrawRecordCount;
        }

        std::size_t CountEnvironmentObjectViewPacketInstances(std::span<const EnvironmentObjectRenderViewPacket> ViewPackets) {
            std::size_t InstanceCount{};
            for (const EnvironmentObjectRenderViewPacket& ViewPacket : ViewPackets) {
                if (ViewPacket.mPacket == nullptr) {
                    continue;
                }

                InstanceCount += ViewPacket.mPacket->mInstanceContexts.size();
            }

            return InstanceCount;
        }
    }

    std::uint32_t GetEnvironmentObjectMainVisibilityMaskBit() {
        return GetEnvironmentRenderMainVisibilityMaskBit();
    }

    std::uint32_t BuildEnvironmentObjectShadowVisibilityMaskBit(std::uint32_t CascadeIndex) {
        return BuildEnvironmentRenderShadowVisibilityMaskBit(CascadeIndex);
    }

    std::uint32_t BuildEnvironmentObjectVisibilityMask(bool IsMainVisible, std::uint32_t ShadowCascadeMask) {
        return BuildEnvironmentRenderVisibilityMask(IsMainVisible, ShadowCascadeMask);
    }

    EnvironmentObjectRenderPacket BuildEnvironmentObjectRenderPacket(const EnvironmentObjectCell& Cell) {
        EnvironmentObjectRenderPacket Packet{};
        Packet.mCellKey = Cell.mKey;
        Packet.mWorldBoundingBox = Cell.mWorldBoundingBox;
        Packet.mGenerationVersion = Cell.mGenerationVersion;
        Packet.mLastTouchedFrame = Cell.mLastTouchedFrame;
        Packet.mHasWorldBoundingBox = Cell.mHasWorldBoundingBox;

        if (Cell.mInstances.empty() == true || Cell.mBatchesByLodLevel.empty() == true) {
            return Packet;
        }

        bool HasRenderableLod{};
        for (const std::vector<EnvironmentObjectBatch>& Batches : Cell.mBatchesByLodLevel) {
            if (HasRenderableBatch(Batches) == true) {
                HasRenderableLod = true;
                break;
            }
        }

        if (HasRenderableLod == false) {
            return Packet;
        }

        Packet.mInstanceContexts.reserve(Cell.mInstances.size());
        for (const EnvironmentObjectInstance& Instance : Cell.mInstances) {
            Packet.mInstanceContexts.push_back(BuildEnvironmentInstanceContext(Instance));
            Packet.mInstancePrototypeIndices.push_back(Instance.mPrototypeIndex);
            Packet.mPrototypeIndices.push_back(Instance.mPrototypeIndex);
        }

        std::sort(Packet.mPrototypeIndices.begin(), Packet.mPrototypeIndices.end());
        Packet.mPrototypeIndices.erase(std::unique(Packet.mPrototypeIndices.begin(), Packet.mPrototypeIndices.end()), Packet.mPrototypeIndices.end());

        Packet.mLods.resize(Cell.mBatchesByLodLevel.size());
        for (std::size_t LodLevel{}; LodLevel < Cell.mBatchesByLodLevel.size(); LodLevel += 1ULL) {
            const std::vector<EnvironmentObjectBatch>& Batches{ Cell.mBatchesByLodLevel[LodLevel] };
            EnvironmentObjectRenderPacketLod& PacketLod{ Packet.mLods[LodLevel] };
            PacketLod.mSegmentContexts.reserve(Batches.size());
            PacketLod.mDrawRecords.reserve(Batches.size());

            for (const EnvironmentObjectBatch& Batch : Batches) {
                if (IsRenderableBatch(Batch) == false) {
                    continue;
                }

                const std::uint32_t SegmentContextIndex{ static_cast<std::uint32_t>(PacketLod.mSegmentContexts.size()) };
                PacketLod.mSegmentContexts.push_back(BuildEnvironmentSegmentContext(Batch));
                PacketLod.mDrawRecords.push_back(BuildEnvironmentDrawRecord(Batch, SegmentContextIndex));
            }
        }

        return Packet;
    }

    bool EmptyEnvironmentObjectRenderPacket(const EnvironmentObjectRenderPacket& Packet) {
        if (Packet.mInstanceContexts.empty() == true) {
            return true;
        }

        for (const EnvironmentObjectRenderPacketLod& Lod : Packet.mLods) {
            if (Lod.mDrawRecords.empty() == false) {
                return false;
            }
        }

        return true;
    }

    void AppendEnvironmentObjectRenderViewData(std::span<const EnvironmentObjectRenderViewPacket> ViewPackets, RenderContract::RenderGatherResult& OutRenderGatherResult) {
        if (ViewPackets.empty() == true) {
            return;
        }

        EnvironmentDrawBucketBuilder BucketBuilder{};
        BucketBuilder.Reserve(CountEnvironmentObjectViewPacketDrawRecords(ViewPackets), CountEnvironmentObjectViewPacketInstances(ViewPackets));
        for (const EnvironmentObjectRenderViewPacket& ViewPacket : ViewPackets) {
            AppendEnvironmentObjectViewPacketToBucketBuilder(ViewPacket, BucketBuilder);
        }

        BucketBuilder.AppendToRenderGatherResult(OutRenderGatherResult);
    }

    void AppendEnvironmentObjectRenderPacketDataLegacy(const EnvironmentObjectRenderPacket& Packet, const EnvironmentObjectRenderBuildOptions& Options, RenderContract::RenderGatherResult& OutRenderGatherResult) {
        if (EmptyEnvironmentObjectRenderPacket(Packet) == true || Packet.mLods.empty() == true || (Options.mEnableMainPass == false && Options.mEnableShadowPass == false)) {
            return;
        }

        std::vector<RenderContract::EnvironmentInstanceContext>& EnvironmentInstanceContexts{ OutRenderGatherResult.GetEnvironmentInstanceContexts() };
        std::vector<RenderContract::EnvironmentSegmentContext>& EnvironmentSegmentContexts{ OutRenderGatherResult.GetEnvironmentSegmentContexts() };
        std::vector<RenderContract::EnvironmentDrawRecord>& EnvironmentDrawRecords{ OutRenderGatherResult.GetEnvironmentDrawRecords() };
        const std::size_t LodLevel{ std::min<std::size_t>(Options.mLodLevel, Packet.mLods.size() - 1ULL) };
        const EnvironmentObjectRenderPacketLod& Lod{ Packet.mLods[LodLevel] };
        if (Lod.mDrawRecords.empty() == true) {
            return;
        }

        const std::size_t InstanceContextOffset{ EnvironmentInstanceContexts.size() };
        const std::size_t SegmentContextOffset{ EnvironmentSegmentContexts.size() };
        EnvironmentInstanceContexts.reserve(EnvironmentInstanceContexts.size() + Packet.mInstanceContexts.size());
        EnvironmentInstanceContexts.insert(EnvironmentInstanceContexts.end(), Packet.mInstanceContexts.begin(), Packet.mInstanceContexts.end());
        EnvironmentSegmentContexts.reserve(EnvironmentSegmentContexts.size() + Lod.mSegmentContexts.size());
        EnvironmentSegmentContexts.insert(EnvironmentSegmentContexts.end(), Lod.mSegmentContexts.begin(), Lod.mSegmentContexts.end());
        EnvironmentDrawRecords.reserve(EnvironmentDrawRecords.size() + Lod.mDrawRecords.size());

        for (const RenderContract::EnvironmentDrawRecord& PacketDrawRecord : Lod.mDrawRecords) {
            const RenderContract::EnvironmentDrawRecord DrawRecord{ BuildOffsetEnvironmentDrawRecord(PacketDrawRecord, InstanceContextOffset, SegmentContextOffset) };
            if (Options.mEnableMainPass == true) {
                EnvironmentDrawRecords.push_back(DrawRecord);
            }

            if (Options.mEnableShadowPass == false || DrawRecord.mCastsShadow == false) {
                continue;
            }

            for (std::uint32_t CascadeIndex{}; CascadeIndex < RenderContract::ShadowCascadeMaxCount; CascadeIndex += 1u) {
                const std::uint32_t CascadeBit{ 1u << CascadeIndex };
                if (((Options.mShadowCascadeMask & DrawRecord.mShadowCascadeMask) & CascadeBit) != 0u) {
                    OutRenderGatherResult.GetShadowRenderContexts()[CascadeIndex].mEnvironmentDrawRecords.push_back(DrawRecord);
                }
            }
        }
    }

    void AppendEnvironmentObjectCellRenderDataLegacy(const EnvironmentObjectCell& Cell, const EnvironmentObjectRenderBuildOptions& Options, RenderContract::RenderGatherResult& OutRenderGatherResult) {
        const EnvironmentObjectRenderPacket Packet{ BuildEnvironmentObjectRenderPacket(Cell) };
        AppendEnvironmentObjectRenderPacketDataLegacy(Packet, Options, OutRenderGatherResult);
    }

    void AppendEnvironmentObjectRenderPacketData(const EnvironmentObjectRenderPacket& Packet, const EnvironmentObjectRenderBuildOptions& Options, RenderContract::RenderGatherResult& OutRenderGatherResult) {
        if (EmptyEnvironmentObjectRenderPacket(Packet) == true || Packet.mLods.empty() == true || (Options.mEnableMainPass == false && Options.mEnableShadowPass == false)) {
            return;
        }

        std::uint32_t MaximumPrototypeIndex{};
        for (std::uint32_t PrototypeIndex : Packet.mPrototypeIndices) {
            MaximumPrototypeIndex = std::max(MaximumPrototypeIndex, PrototypeIndex);
        }

        const std::uint32_t SelectedLodLevel{ std::min<std::uint32_t>(Options.mLodLevel, static_cast<std::uint32_t>(Packet.mLods.size() - 1ULL)) };
        std::vector<std::uint32_t> PrototypeLodLevels{};
        PrototypeLodLevels.resize(Packet.mPrototypeIndices.empty() == true ? 0ULL : static_cast<std::size_t>(MaximumPrototypeIndex) + 1ULL, SelectedLodLevel);

        std::uint32_t VisibilityMask{};
        if (Options.mEnableMainPass == true) {
            VisibilityMask |= GetEnvironmentRenderMainVisibilityMaskBit();
        }

        if (Options.mEnableShadowPass == true) {
            VisibilityMask |= BuildEnvironmentRenderVisibilityMask(false, Options.mShadowCascadeMask);
        }

        EnvironmentObjectRenderViewPacket ViewPacket{};
        ViewPacket.mPacket = &Packet;
        ViewPacket.mPrototypeLodLevels = PrototypeLodLevels.empty() == true ? nullptr : PrototypeLodLevels.data();
        ViewPacket.mPrototypeLodLevelCount = PrototypeLodLevels.size();
        ViewPacket.mVisibilityMask = VisibilityMask;
        AppendEnvironmentObjectRenderViewData(std::span<const EnvironmentObjectRenderViewPacket>{ &ViewPacket, 1ULL }, OutRenderGatherResult);
    }

    void AppendEnvironmentObjectCellRenderData(const EnvironmentObjectCell& Cell, const EnvironmentObjectRenderBuildOptions& Options, RenderContract::RenderGatherResult& OutRenderGatherResult) {
        const EnvironmentObjectRenderPacket Packet{ BuildEnvironmentObjectRenderPacket(Cell) };
        AppendEnvironmentObjectRenderPacketData(Packet, Options, OutRenderGatherResult);
    }
}
