#include "PipelineExecutor.h"
#include <cstddef>
#include <vector>
#include "Game/Scene/Pipeline/PipelineContext.h"

namespace Game {
    namespace Pipeline {
        namespace {
            void ExecuteWorkUnit(Arche::World& World, SceneWorkUnit& WorkUnit, float Dt);
            void ExecuteWorkUnitRange(Arche::World& World, std::span<SceneWorkUnit> WorkUnits, std::size_t BeginIndex, std::size_t EndIndex, float Dt);

            void ExecuteWorkUnit(Arche::World& World, SceneWorkUnit& WorkUnit, float Dt) {
                WorkUnit.GetRenderGatherResult().Clear();
                std::vector<Arche::EntityID>& EntityIds{ WorkUnit.GetEntityIds() };
                PipelineContext Ctx{ World, WorkUnit.GetUnitEntityId(), std::span<const Arche::EntityID>{ EntityIds.data(), EntityIds.size() }, WorkUnit.GetRenderGatherResult() };

                for (IPipelineSystem* PipelineSystem : WorkUnit.GetPipelineSystems()) {
                    if (PipelineSystem == nullptr) {
                        continue;
                    }

                    PipelineSystem->Execute(Ctx, Dt);
                }
            }

            void ExecuteWorkUnitRange(Arche::World& World, std::span<SceneWorkUnit> WorkUnits, std::size_t BeginIndex, std::size_t EndIndex, float Dt) {
                for (std::size_t WorkUnitIndex{ BeginIndex }; WorkUnitIndex < EndIndex; ++WorkUnitIndex) {
                    ExecuteWorkUnit(World, WorkUnits[WorkUnitIndex], Dt);
                }
            }
        }

        PipelineExecutor::PipelineExecutor()
            : mThreadPool{} {
        }

        PipelineExecutor::~PipelineExecutor() {
        }

        PipelineExecutor::PipelineExecutor(const PipelineExecutor& Other)
            : mThreadPool{} {
            (void)Other;
        }

        PipelineExecutor& PipelineExecutor::operator=(const PipelineExecutor& Other) {
            (void)Other;
            return *this;
        }

        PipelineExecutor::PipelineExecutor(PipelineExecutor&& Other) noexcept
            : mThreadPool{} {
            (void)Other;
        }

        PipelineExecutor& PipelineExecutor::operator=(PipelineExecutor&& Other) noexcept {
            (void)Other;
            return *this;
        }

        void PipelineExecutor::Execute(Arche::World& World, std::span<SceneWorkUnit> WorkUnits, float Dt) {
            const std::size_t WorkUnitCount{ WorkUnits.size() };
            if (WorkUnitCount == 0) {
                return;
            }

            if (WorkUnitCount == 1) {
                ExecuteWorkUnit(World, WorkUnits[0], Dt);
                return;
            }

            const auto BlockFunction{ [&World, WorkUnits, Dt](std::size_t BeginIndex, std::size_t EndIndex) {
                ExecuteWorkUnitRange(World, WorkUnits, BeginIndex, EndIndex, Dt);
            } };
            BS::multi_future<void> WorkUnitFutures{ mThreadPool.submit_blocks<std::size_t>(0, WorkUnitCount, BlockFunction) };
            WorkUnitFutures.wait();
        }
    }
}
