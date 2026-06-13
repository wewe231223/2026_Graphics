#include "EnvironmentObjectRenderDataBuilder.h"

#include <algorithm>
#include <array>
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

        RFD::EnvironmentDrawRecord BuildEnvironmentDrawRecord(const EnvironmentObjectBatch& Batch, std::uint32_t InstanceBaseOffset, std::uint32_t SegmentContextIndex) {
            RFD::EnvironmentDrawRecord DrawRecord{};
            DrawRecord.mPipeline = Batch.mPipeline;
            DrawRecord.mMesh = Batch.mMesh;
            DrawRecord.mSubMesh = Batch.mSubMeshIndex;
            DrawRecord.mPass = 0u;
            DrawRecord.mInstanceOffset = InstanceBaseOffset + Batch.mInstanceOffsetInCell;
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
    }

    void AppendEnvironmentObjectCellRenderData(const EnvironmentObjectCell& Cell, const EnvironmentObjectRenderBuildOptions& Options, Pipeline::RenderGatherResult& OutRenderGatherResult) {
        if (Cell.mInstances.empty() == true || Cell.mBatchesByLodLevel.empty() == true || (Options.mEnableMainPass == false && Options.mEnableShadowPass == false)) {
            return;
        }

        const std::size_t LodLevel{ std::min<std::size_t>(Options.mLodLevel, Cell.mBatchesByLodLevel.size() - 1ULL) };
        const std::vector<EnvironmentObjectBatch>& Batches{ Cell.mBatchesByLodLevel[LodLevel] };
        if (HasRenderableBatch(Batches) == false) {
            return;
        }

        const std::uint32_t InstanceBaseOffset{ static_cast<std::uint32_t>(OutRenderGatherResult.GetEnvironmentInstanceContexts().size()) };
        OutRenderGatherResult.GetEnvironmentInstanceContexts().reserve(OutRenderGatherResult.GetEnvironmentInstanceContexts().size() + Cell.mInstances.size());
        for (const EnvironmentObjectInstance& Instance : Cell.mInstances) {
            OutRenderGatherResult.GetEnvironmentInstanceContexts().push_back(BuildEnvironmentInstanceContext(Instance));
        }

        OutRenderGatherResult.GetEnvironmentSegmentContexts().reserve(OutRenderGatherResult.GetEnvironmentSegmentContexts().size() + Batches.size());
        OutRenderGatherResult.GetEnvironmentDrawRecords().reserve(OutRenderGatherResult.GetEnvironmentDrawRecords().size() + Batches.size());

        for (const EnvironmentObjectBatch& Batch : Batches) {
            if (IsRenderableBatch(Batch) == false) {
                continue;
            }

            const std::uint32_t SegmentContextIndex{ static_cast<std::uint32_t>(OutRenderGatherResult.GetEnvironmentSegmentContexts().size()) };
            OutRenderGatherResult.GetEnvironmentSegmentContexts().push_back(BuildEnvironmentSegmentContext(Batch));
            const RFD::EnvironmentDrawRecord DrawRecord{ BuildEnvironmentDrawRecord(Batch, InstanceBaseOffset, SegmentContextIndex) };
            if (Options.mEnableMainPass == true) {
                OutRenderGatherResult.GetEnvironmentDrawRecords().push_back(DrawRecord);
            }

            if (Options.mEnableShadowPass == false) {
                continue;
            }

            std::array<RFD::ShadowRenderContext, RFD::ShadowCascadeMaxCount>& ShadowRenderContexts{ OutRenderGatherResult.GetShadowRenderContexts() };
            for (std::uint32_t CascadeIndex{}; CascadeIndex < ShadowRenderContexts.size(); CascadeIndex += 1u) {
                const std::uint32_t CascadeBit{ 1u << CascadeIndex };
                if ((Options.mShadowCascadeMask & CascadeBit) == 0u) {
                    continue;
                }

                ShadowRenderContexts[CascadeIndex].mEnvironmentDrawRecords.push_back(DrawRecord);
            }
        }
    }
}
