#include "PipelineExecutor.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <future>
#include <vector>
#include "Game/Scene/Pipeline/PipelineContext.h"

namespace Game {
    namespace Pipeline {
        namespace {
            void ExecuteWorkUnit(Arche::World& World, SceneWorkUnit& WorkUnit, const PipelineFrameInput& FrameInput, float Dt);
            std::size_t ResolveTotalWorkerCount(std::size_t WorkUnitCount, std::size_t ThreadPoolWorkerCount);
            void ExecuteWorkUnitsByDynamicPull(Arche::World& World, std::span<SceneWorkUnit> WorkUnits, const PipelineFrameInput& FrameInput, float Dt, BS::thread_pool<BS::tp::none>& ThreadPool);

            void ExecuteWorkUnit(Arche::World& World, SceneWorkUnit& WorkUnit, const PipelineFrameInput& FrameInput, float Dt) {
                WorkUnit.GetRenderGatherResult().Clear();
                std::vector<Arche::EntityID>& EntityIds{ WorkUnit.GetEntityIds() };
                PipelineContext Ctx{ World, WorkUnit.GetUnitEntityId(), std::span<const Arche::EntityID>{ EntityIds.data(), EntityIds.size() }, FrameInput, WorkUnit.GetRenderGatherResult() };

                for (IPipelineSystem* PipelineSystem : WorkUnit.GetPipelineSystems()) {
                    if (PipelineSystem == nullptr) {
                        continue;
                    }

                    PipelineSystem->Execute(Ctx, Dt);
                }
            }

            std::size_t ResolveTotalWorkerCount(std::size_t WorkUnitCount, std::size_t ThreadPoolWorkerCount) {
                if (WorkUnitCount == 0) {
                    return 0;
                }

                if (ThreadPoolWorkerCount == 0) {
                    return 1;
                }

                return std::min(WorkUnitCount, ThreadPoolWorkerCount);
            }

            void ExecuteWorkUnitsByDynamicPull(Arche::World& World, std::span<SceneWorkUnit> WorkUnits, const PipelineFrameInput& FrameInput, float Dt, BS::thread_pool<BS::tp::none>& ThreadPool) {
                const std::size_t WorkUnitCount{ WorkUnits.size() };
                const std::size_t TotalWorkerCount{ ResolveTotalWorkerCount(WorkUnitCount, ThreadPool.get_thread_count()) };
                const std::size_t SubmittedWorkerCount{ TotalWorkerCount > 1 ? TotalWorkerCount - 1 : 0 };
                std::atomic<std::size_t> NextWorkUnitIndex{};
                auto WorkerFunction{ [&World, WorkUnits, &FrameInput, Dt, WorkUnitCount, &NextWorkUnitIndex]() {
                    while (true) {
                        const std::size_t WorkUnitIndex{ NextWorkUnitIndex.fetch_add(1, std::memory_order_relaxed) };
                        if (WorkUnitIndex >= WorkUnitCount) {
                            break;
                        }

                        ExecuteWorkUnit(World, WorkUnits[WorkUnitIndex], FrameInput, Dt);
                    }
                } };

                std::vector<std::future<void>> WorkerFutures{};
                WorkerFutures.reserve(SubmittedWorkerCount);
                for (std::size_t WorkerIndex{}; WorkerIndex < SubmittedWorkerCount; ++WorkerIndex) {
                    WorkerFutures.push_back(ThreadPool.submit_task(WorkerFunction));
                }

                WorkerFunction();

                for (std::future<void>& WorkerFuture : WorkerFutures) {
                    WorkerFuture.wait();
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

        void PipelineExecutor::Execute(Arche::World& World, std::span<SceneWorkUnit> WorkUnits, const PipelineFrameInput& FrameInput, float Dt) {
            const std::size_t WorkUnitCount{ WorkUnits.size() };
            if (WorkUnitCount == 0) {
                return;
            }

            if (WorkUnitCount == 1) {
                ExecuteWorkUnit(World, WorkUnits[0], FrameInput, Dt);
                return;
            }

            ExecuteWorkUnitsByDynamicPull(World, WorkUnits, FrameInput, Dt, mThreadPool);
        }
    }
}
