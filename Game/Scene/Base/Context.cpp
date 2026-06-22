#include "Context.h"
#include <utility>

namespace Game {
    namespace Pipeline {
        PipelineContext::PipelineContext(Arche::World& World, Arche::EntityID UnitEntityId, std::span<const Arche::EntityID> EntityIds, const PipelineFrameInput& FrameInput, RenderContract::RenderGatherResult& RenderGatherResultValue)
            : mWorld{ &World },
            mUnitEntityId{ UnitEntityId },
            mEntityIds{ EntityIds },
            mFrameInput{ FrameInput },
            mRenderGatherResult{ &RenderGatherResultValue } {
        }

        PipelineContext::~PipelineContext() {
        }

        PipelineContext::PipelineContext(const PipelineContext& Other)
            : mWorld{ Other.mWorld },
            mUnitEntityId{ Other.mUnitEntityId },
            mEntityIds{ Other.mEntityIds },
            mFrameInput{ Other.mFrameInput },
            mRenderGatherResult{ Other.mRenderGatherResult } {
        }

        PipelineContext& PipelineContext::operator=(const PipelineContext& Other) {
            if (this == &Other) {
                return *this;
            }

            mWorld = Other.mWorld;
            mUnitEntityId = Other.mUnitEntityId;
            mEntityIds = Other.mEntityIds;
            mFrameInput = Other.mFrameInput;
            mRenderGatherResult = Other.mRenderGatherResult;
            return *this;
        }

        PipelineContext::PipelineContext(PipelineContext&& Other) noexcept
            : mWorld{ std::move(Other.mWorld) },
            mUnitEntityId{ std::move(Other.mUnitEntityId) },
            mEntityIds{ std::move(Other.mEntityIds) },
            mFrameInput{ std::move(Other.mFrameInput) },
            mRenderGatherResult{ std::move(Other.mRenderGatherResult) } {
        }

        PipelineContext& PipelineContext::operator=(PipelineContext&& Other) noexcept {
            if (this == &Other) {
                return *this;
            }

            mWorld = std::move(Other.mWorld);
            mUnitEntityId = std::move(Other.mUnitEntityId);
            mEntityIds = std::move(Other.mEntityIds);
            mFrameInput = std::move(Other.mFrameInput);
            mRenderGatherResult = std::move(Other.mRenderGatherResult);
            return *this;
        }

        Arche::EntityID PipelineContext::GetUnitEntityId() const {
            return mUnitEntityId;
        }

        Arche::World& PipelineContext::GetWorld() {
            return *mWorld;
        }

        const Arche::World& PipelineContext::GetWorld() const {
            return *mWorld;
        }

        const PipelineFrameInput& PipelineContext::GetFrameInput() const {
            return mFrameInput;
        }

        const std::vector<RegisteredMaterialGroup>* PipelineContext::GetMaterialGroups() const {
            return mFrameInput.mMaterialGroups;
        }

        const Terrain::ITerrainQuery* PipelineContext::GetTerrainQuery() const {
            return mFrameInput.mTerrainQueryResource;
        }

        Arche::EntityID PipelineContext::GetPickedEntityId() const {
            return mFrameInput.mPickedEntityId;
        }

        std::uint32_t PipelineContext::GetFrameIndex() const {
            return mFrameInput.mFrameIndex;
        }

        std::uint32_t PipelineContext::GetRenderFlags() const {
            return mFrameInput.mRenderFlags;
        }

        bool PipelineContext::HasRenderFlag(std::uint32_t Flag) const {
            return (mFrameInput.mRenderFlags & Flag) != 0u;
        }

        const RenderContract::ShadowMappingParameter& PipelineContext::GetShadowMappingParameter() const {
            return mFrameInput.mShadowMappingParameter;
        }

        const Frustum* PipelineContext::GetActiveCameraFrustum() const {
            if (mFrameInput.mHasActiveCameraFrustum == false) {
                return nullptr;
            }

            return &mFrameInput.mActiveCameraFrustum;
        }

        bool PipelineContext::GetActiveCameraPosition(SimpleMath::Vector3& OutCameraPosition) const {
            if (mFrameInput.mHasActiveCameraPosition == false) {
                return false;
            }

            OutCameraPosition = mFrameInput.mActiveCameraPosition;
            return true;
        }

        void PipelineContext::AddRenderResult(const RenderContract::RenderGatherResult& RenderResult) {
            if (mRenderGatherResult == nullptr) {
                return;
            }

            mRenderGatherResult->Append(RenderResult);
        }

        void PipelineContext::AddRenderResult(RenderContract::RenderGatherResult&& RenderResult) {
            if (mRenderGatherResult == nullptr) {
                return;
            }

            mRenderGatherResult->Append(std::move(RenderResult));
        }

        RenderContract::RenderGatherResult& PipelineContext::GetRenderGatherResult() {
            return *mRenderGatherResult;
        }

        const RenderContract::RenderGatherResult& PipelineContext::GetRenderGatherResult() const {
            return *mRenderGatherResult;
        }
    }
}
