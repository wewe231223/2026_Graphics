#include "EnvironmentObjectRenderDataBuilder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace Game {
    namespace {
        RFD::EnvironmentInstanceContext BuildEnvironmentInstanceContext(const EnvironmentObjectInstance& Instance) {
            RFD::EnvironmentInstanceContext Context{};
            Context.mPositionScale = SimpleMath::Vector4{ Instance.mPosition.x, Instance.mPosition.y, Instance.mPosition.z, Instance.mScale };
            Context.mRotationVariation = SimpleMath::Vector4{ Instance.mYawRadians, static_cast<float>(Instance.mVariation), 0.0f, 0.0f };
            return Context;
        }

        RFD::EnvironmentSegmentContext BuildEnvironmentSegmentContext(const EnvironmentObjectBatch& Batch) {
            RFD::EnvironmentSegmentContext Context{};
            Context.mLocalTransform = Batch.mLocalTransform;
            return Context;
        }

        RFD::EnvironmentDrawRecord BuildEnvironmentDrawRecord(const EnvironmentObjectBatch& Batch, std::uint32_t SegmentContextIndex) {
            RFD::EnvironmentDrawRecord DrawRecord{};
            DrawRecord.mPipeline = Batch.mPipeline;
            DrawRecord.mMesh = Batch.mMesh;
            DrawRecord.mSubMesh = Batch.mSubMeshIndex;
            DrawRecord.mPass = 0u;
            DrawRecord.mInstanceOffset = Batch.mInstanceOffsetInCell;
            DrawRecord.mInstanceCount = Batch.mInstanceCount;
            DrawRecord.mSegmentContextIndex = SegmentContextIndex;
            DrawRecord.mMaterialIndex = Batch.mMaterialIndex;
            DrawRecord.mFlags = Batch.mFlags;
            return DrawRecord;
        }

        bool IsRenderableBatch(const EnvironmentObjectBatch& Batch) {
            return Batch.mPipeline != nullptr && Batch.mMesh != nullptr && Batch.mInstanceCount > 0u;
        }

        bool HasRenderableBatch(const std::vector<EnvironmentObjectBatch>& Batches) {
            for (const EnvironmentObjectBatch& Batch : Batches) {
                if (IsRenderableBatch(Batch) == true) {
                    return true;
                }
            }

            return false;
        }

        RFD::EnvironmentDrawRecord BuildOffsetEnvironmentDrawRecord(RFD::EnvironmentDrawRecord DrawRecord, std::size_t InstanceContextOffset, std::size_t SegmentContextOffset) {
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

    void AppendEnvironmentObjectRenderPacketData(const EnvironmentObjectRenderPacket& Packet, const EnvironmentObjectRenderBuildOptions& Options, Pipeline::RenderGatherResult& OutRenderGatherResult) {
        if (EmptyEnvironmentObjectRenderPacket(Packet) == true || Packet.mLods.empty() == true || (Options.mEnableMainPass == false && Options.mEnableShadowPass == false)) {
            return;
        }

        const std::size_t LodLevel{ std::min<std::size_t>(Options.mLodLevel, Packet.mLods.size() - 1ULL) };
        const EnvironmentObjectRenderPacketLod& Lod{ Packet.mLods[LodLevel] };
        if (Lod.mDrawRecords.empty() == true) {
            return;
        }

        const std::size_t InstanceContextOffset{ OutRenderGatherResult.GetEnvironmentInstanceContexts().size() };
        const std::size_t SegmentContextOffset{ OutRenderGatherResult.GetEnvironmentSegmentContexts().size() };

        OutRenderGatherResult.GetEnvironmentInstanceContexts().reserve(OutRenderGatherResult.GetEnvironmentInstanceContexts().size() + Packet.mInstanceContexts.size());
        OutRenderGatherResult.GetEnvironmentInstanceContexts().insert(OutRenderGatherResult.GetEnvironmentInstanceContexts().end(), Packet.mInstanceContexts.begin(), Packet.mInstanceContexts.end());
        OutRenderGatherResult.GetEnvironmentSegmentContexts().reserve(OutRenderGatherResult.GetEnvironmentSegmentContexts().size() + Lod.mSegmentContexts.size());
        OutRenderGatherResult.GetEnvironmentSegmentContexts().insert(OutRenderGatherResult.GetEnvironmentSegmentContexts().end(), Lod.mSegmentContexts.begin(), Lod.mSegmentContexts.end());
        OutRenderGatherResult.GetEnvironmentDrawRecords().reserve(OutRenderGatherResult.GetEnvironmentDrawRecords().size() + Lod.mDrawRecords.size());

        std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& ShadowRenderContexts{ OutRenderGatherResult.GetShadowRenderContexts() };
        if (Options.mEnableShadowPass == true) {
            for (std::uint32_t CascadeIndex{}; CascadeIndex < ShadowRenderContexts.size(); CascadeIndex += 1u) {
                const std::uint32_t CascadeBit{ 1u << CascadeIndex };
                if ((Options.mShadowCascadeMask & CascadeBit) != 0u) {
                    ShadowRenderContexts[CascadeIndex].mEnvironmentDrawRecords.reserve(ShadowRenderContexts[CascadeIndex].mEnvironmentDrawRecords.size() + Lod.mDrawRecords.size());
                }
            }
        }

        for (const RFD::EnvironmentDrawRecord& PacketDrawRecord : Lod.mDrawRecords) {
            const RFD::EnvironmentDrawRecord DrawRecord{ BuildOffsetEnvironmentDrawRecord(PacketDrawRecord, InstanceContextOffset, SegmentContextOffset) };
            if (Options.mEnableMainPass == true) {
                OutRenderGatherResult.GetEnvironmentDrawRecords().push_back(DrawRecord);
            }

            if (Options.mEnableShadowPass == false) {
                continue;
            }

            for (std::uint32_t CascadeIndex{}; CascadeIndex < ShadowRenderContexts.size(); CascadeIndex += 1u) {
                const std::uint32_t CascadeBit{ 1u << CascadeIndex };
                if ((Options.mShadowCascadeMask & CascadeBit) != 0u) {
                    ShadowRenderContexts[CascadeIndex].mEnvironmentDrawRecords.push_back(DrawRecord);
                }
            }
        }
    }

    void AppendEnvironmentObjectCellRenderData(const EnvironmentObjectCell& Cell, const EnvironmentObjectRenderBuildOptions& Options, Pipeline::RenderGatherResult& OutRenderGatherResult) {
        const EnvironmentObjectRenderPacket Packet{ BuildEnvironmentObjectRenderPacket(Cell) };
        AppendEnvironmentObjectRenderPacketData(Packet, Options, OutRenderGatherResult);
    }
}
