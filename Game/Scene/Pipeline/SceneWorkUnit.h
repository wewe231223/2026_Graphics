#pragma once
#include <vector>
#include "Arche/Common.h"
#include "Game/Scene/Pipeline/PipelineSystem.h"
#include "Game/Scene/Pipeline/PipelineTypes.h"

namespace Game {
    namespace Pipeline {
        class SceneWorkUnit final {
        public:
            SceneWorkUnit();
            ~SceneWorkUnit();

            SceneWorkUnit(const SceneWorkUnit& Other);
            SceneWorkUnit& operator=(const SceneWorkUnit& Other);

            SceneWorkUnit(SceneWorkUnit&& Other) noexcept;
            SceneWorkUnit& operator=(SceneWorkUnit&& Other) noexcept;

        public:
            Arche::EntityID GetUnitEntityId() const;
            void SetUnitEntityId(Arche::EntityID UnitEntityId);

            std::vector<Arche::EntityID>& GetEntityIds();
            const std::vector<Arche::EntityID>& GetEntityIds() const;

            PipelineId GetPipelineId() const;
            void SetPipelineId(PipelineId PipelineIdValue);

            std::vector<IPipelineSystem*>& GetPipelineSystems();
            const std::vector<IPipelineSystem*>& GetPipelineSystems() const;

        private:
            Arche::EntityID mUnitEntityId{ Arche::NullEntityID };
            std::vector<Arche::EntityID> mEntityIds{};
            PipelineId mPipelineId{ InvalidPipelineId };
            std::vector<IPipelineSystem*> mPipelineSystems{};
        };
    }
}
