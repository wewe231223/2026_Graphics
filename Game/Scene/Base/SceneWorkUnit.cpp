#include "SceneWorkUnit.h"
#include <utility>

namespace Game {
    namespace Pipeline {
        SceneWorkUnit::SceneWorkUnit()
            : mUnitEntityId{ Arche::NullEntityID },
            mEntityIds{},
            mPipelineId{ InvalidPipelineId },
            mPipelineSystems{} {
        }

        SceneWorkUnit::~SceneWorkUnit() {
        }

        SceneWorkUnit::SceneWorkUnit(const SceneWorkUnit& Other)
            : mUnitEntityId{ Other.mUnitEntityId },
            mEntityIds{ Other.mEntityIds },
            mPipelineId{ Other.mPipelineId },
            mPipelineSystems{ Other.mPipelineSystems } {
        }

        SceneWorkUnit& SceneWorkUnit::operator=(const SceneWorkUnit& Other) {
            if (this == &Other) {
                return *this;
            }

            mUnitEntityId = Other.mUnitEntityId;
            mEntityIds = Other.mEntityIds;
            mPipelineId = Other.mPipelineId;
            mPipelineSystems = Other.mPipelineSystems;
            return *this;
        }

        SceneWorkUnit::SceneWorkUnit(SceneWorkUnit&& Other) noexcept
            : mUnitEntityId{ std::move(Other.mUnitEntityId) },
            mEntityIds{ std::move(Other.mEntityIds) },
            mPipelineId{ std::move(Other.mPipelineId) },
            mPipelineSystems{ std::move(Other.mPipelineSystems) } {
        }

        SceneWorkUnit& SceneWorkUnit::operator=(SceneWorkUnit&& Other) noexcept {
            if (this == &Other) {
                return *this;
            }

            mUnitEntityId = std::move(Other.mUnitEntityId);
            mEntityIds = std::move(Other.mEntityIds);
            mPipelineId = std::move(Other.mPipelineId);
            mPipelineSystems = std::move(Other.mPipelineSystems);
            return *this;
        }

        Arche::EntityID SceneWorkUnit::GetUnitEntityId() const {
            return mUnitEntityId;
        }

        void SceneWorkUnit::SetUnitEntityId(Arche::EntityID UnitEntityId) {
            mUnitEntityId = UnitEntityId;
        }

        std::vector<Arche::EntityID>& SceneWorkUnit::GetEntityIds() {
            return mEntityIds;
        }

        const std::vector<Arche::EntityID>& SceneWorkUnit::GetEntityIds() const {
            return mEntityIds;
        }

        PipelineId SceneWorkUnit::GetPipelineId() const {
            return mPipelineId;
        }

        void SceneWorkUnit::SetPipelineId(PipelineId PipelineIdValue) {
            mPipelineId = PipelineIdValue;
        }

        std::vector<IPipelineSystem*>& SceneWorkUnit::GetPipelineSystems() {
            return mPipelineSystems;
        }

        const std::vector<IPipelineSystem*>& SceneWorkUnit::GetPipelineSystems() const {
            return mPipelineSystems;
        }
    }
}
