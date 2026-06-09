#include "PipelineContext.h"
#include <algorithm>
#include <utility>

namespace Game {
    namespace Pipeline {
        PipelineContext::PipelineContext(Arche::World& World, Arche::EntityID UnitEntityId, std::span<const Arche::EntityID> EntityIds, RenderGatherResult& RenderGatherResult)
            : mWorld{ &World },
            mUnitEntityId{ UnitEntityId },
            mEntityIds{ EntityIds },
            mRenderGatherResult{ &RenderGatherResult } {
        }

        PipelineContext::~PipelineContext() {
        }

        PipelineContext::PipelineContext(const PipelineContext& Other)
            : mWorld{ Other.mWorld },
            mUnitEntityId{ Other.mUnitEntityId },
            mEntityIds{ Other.mEntityIds },
            mRenderGatherResult{ Other.mRenderGatherResult } {
        }

        PipelineContext& PipelineContext::operator=(const PipelineContext& Other) {
            if (this == &Other) {
                return *this;
            }

            mWorld = Other.mWorld;
            mUnitEntityId = Other.mUnitEntityId;
            mEntityIds = Other.mEntityIds;
            mRenderGatherResult = Other.mRenderGatherResult;
            return *this;
        }

        PipelineContext::PipelineContext(PipelineContext&& Other) noexcept
            : mWorld{ std::move(Other.mWorld) },
            mUnitEntityId{ std::move(Other.mUnitEntityId) },
            mEntityIds{ std::move(Other.mEntityIds) },
            mRenderGatherResult{ std::move(Other.mRenderGatherResult) } {
        }

        PipelineContext& PipelineContext::operator=(PipelineContext&& Other) noexcept {
            if (this == &Other) {
                return *this;
            }

            mWorld = std::move(Other.mWorld);
            mUnitEntityId = std::move(Other.mUnitEntityId);
            mEntityIds = std::move(Other.mEntityIds);
            mRenderGatherResult = std::move(Other.mRenderGatherResult);
            return *this;
        }

        Arche::EntityID PipelineContext::GetUnitEntityId() const {
            return mUnitEntityId;
        }

        bool PipelineContext::ContainsEntity(Arche::EntityID EntityId) const {
            if (EntityId == Arche::NullEntityID) {
                return false;
            }

            if (EntityId == mUnitEntityId) {
                return true;
            }

            const std::span<const Arche::EntityID>::iterator EntityIter{ std::find(mEntityIds.begin(), mEntityIds.end(), EntityId) };
            return EntityIter != mEntityIds.end();
        }

        void PipelineContext::AddRenderResult(const RenderGatherResult& RenderResult) {
            if (mRenderGatherResult == nullptr) {
                return;
            }

            mRenderGatherResult->Append(RenderResult);
        }

        void PipelineContext::AddRenderResult(RenderGatherResult&& RenderResult) {
            if (mRenderGatherResult == nullptr) {
                return;
            }

            mRenderGatherResult->Append(std::move(RenderResult));
        }

        RenderGatherResult& PipelineContext::GetRenderGatherResult() {
            return *mRenderGatherResult;
        }

        const RenderGatherResult& PipelineContext::GetRenderGatherResult() const {
            return *mRenderGatherResult;
        }
    }
}
