#include "EnvironmentObjectRenderDataBuilder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "RenderContract/Writer/EnvironmentRenderWriter.h"

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

        RenderContract::EnvironmentDrawRecord BuildOffsetEnvironmentDrawRecord(RenderContract::EnvironmentDrawRecord DrawRecord, std::size_t InstanceContextOffset, std::size_t SegmentContextOffset) {
            DrawRecord.mInstanceOffset += static_cast<std::uint32_t>(InstanceContextOffset);
            DrawRecord.mSegmentContextIndex += static_cast<std::uint32_t>(SegmentContextOffset);
            return DrawRecord;
        }
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

    void AppendEnvironmentObjectRenderPacketData(const EnvironmentObjectRenderPacket& Packet, const EnvironmentObjectRenderBuildOptions& Options, RenderContract::RenderGatherResult& OutRenderGatherResult) {
        if (EmptyEnvironmentObjectRenderPacket(Packet) == true || Packet.mLods.empty() == true || (Options.mEnableMainPass == false && Options.mEnableShadowPass == false)) {
            return;
        }

        RenderContract::EnvironmentRenderWriter EnvironmentWriter{ OutRenderGatherResult };
        std::vector<RenderContract::EnvironmentInstanceContext>& EnvironmentInstanceContexts{ EnvironmentWriter.GetEnvironmentInstanceContexts() };
        std::vector<RenderContract::EnvironmentSegmentContext>& EnvironmentSegmentContexts{ EnvironmentWriter.GetEnvironmentSegmentContexts() };
        std::vector<RenderContract::EnvironmentDrawRecord>& EnvironmentDrawRecords{ EnvironmentWriter.GetEnvironmentDrawRecords() };
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

        if (Options.mEnableShadowPass == true) {
            for (std::uint32_t CascadeIndex{}; CascadeIndex < RenderContract::ShadowCascadeMaxCount; CascadeIndex += 1u) {
                const std::uint32_t CascadeBit{ 1u << CascadeIndex };
                if ((Options.mShadowCascadeMask & CascadeBit) != 0u) {
                    std::vector<RenderContract::EnvironmentDrawRecord>& ShadowEnvironmentDrawRecords{ EnvironmentWriter.GetShadowEnvironmentDrawRecords(CascadeIndex) };
                    ShadowEnvironmentDrawRecords.reserve(ShadowEnvironmentDrawRecords.size() + Lod.mDrawRecords.size());
                }
            }
        }

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
                    EnvironmentWriter.GetShadowEnvironmentDrawRecords(CascadeIndex).push_back(DrawRecord);
                }
            }
        }
    }

    void AppendEnvironmentObjectCellRenderData(const EnvironmentObjectCell& Cell, const EnvironmentObjectRenderBuildOptions& Options, RenderContract::RenderGatherResult& OutRenderGatherResult) {
        const EnvironmentObjectRenderPacket Packet{ BuildEnvironmentObjectRenderPacket(Cell) };
        AppendEnvironmentObjectRenderPacketData(Packet, Options, OutRenderGatherResult);
    }
}
