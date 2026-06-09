#include "PipelineExecutor.h"
#include "Game/Scene/Pipeline/PipelineContext.h"

namespace Game {
    namespace Pipeline {
        PipelineExecutor::PipelineExecutor() {
        }

        PipelineExecutor::~PipelineExecutor() {
        }

        PipelineExecutor::PipelineExecutor(const PipelineExecutor& Other) {
            (void)Other;
        }

        PipelineExecutor& PipelineExecutor::operator=(const PipelineExecutor& Other) {
            (void)Other;
            return *this;
        }

        PipelineExecutor::PipelineExecutor(PipelineExecutor&& Other) noexcept {
            (void)Other;
        }

        PipelineExecutor& PipelineExecutor::operator=(PipelineExecutor&& Other) noexcept {
            (void)Other;
            return *this;
        }

        void PipelineExecutor::Execute(Arche::World& World, std::span<SceneWorkUnit> WorkUnits, float Dt) {
            for (SceneWorkUnit& WorkUnit : WorkUnits) {
                std::vector<Arche::EntityID>& EntityIds{ WorkUnit.GetEntityIds() };
                PipelineContext Ctx{ World, WorkUnit.GetUnitEntityId(), std::span<const Arche::EntityID>{ EntityIds.data(), EntityIds.size() }, WorkUnit.GetRenderGatherResult() };

                for (IPipelineSystem* PipelineSystem : WorkUnit.GetPipelineSystems()) {
                    if (PipelineSystem == nullptr) {
                        continue;
                    }

                    PipelineSystem->Execute(Ctx, Dt);
                }
            }
        }
    }
}
