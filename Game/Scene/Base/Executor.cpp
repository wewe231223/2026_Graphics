#include "Executor.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <future>
#include <vector>
#include "Game/Scene/Base/Context.h"

namespace Game {
    namespace Pipeline {
        PipelineExecutor::PipelineExecutor()
            : mThreadPool{},
            mRenderGatherResults{},
            mActiveRenderGatherResultCount{} {
        }

        PipelineExecutor::~PipelineExecutor() {
        }

        PipelineExecutor::PipelineExecutor(const PipelineExecutor& Other)
            : mThreadPool{},
            mRenderGatherResults{},
            mActiveRenderGatherResultCount{} {
            (void)Other;
        }

        PipelineExecutor& PipelineExecutor::operator=(const PipelineExecutor& Other) {
            (void)Other;
            mRenderGatherResults.clear();
            mActiveRenderGatherResultCount = 0;
            return *this;
        }

        PipelineExecutor::PipelineExecutor(PipelineExecutor&& Other) noexcept
            : mThreadPool{},
            mRenderGatherResults{},
            mActiveRenderGatherResultCount{} {
            (void)Other;
        }

        PipelineExecutor& PipelineExecutor::operator=(PipelineExecutor&& Other) noexcept {
            (void)Other;
            mRenderGatherResults.clear();
            mActiveRenderGatherResultCount = 0;
            return *this;
        }

        void PipelineExecutor::ExecuteWorkUnit(Arche::World& World, SceneWorkUnit& WorkUnit, const PipelineFrameInput& FrameInput, RenderContract::RenderGatherResult& RenderGatherResultValue, float Dt) const {
            std::vector<Arche::EntityID>& EntityIds{ WorkUnit.GetEntityIds() };
            PipelineContext Ctx{ World, WorkUnit.GetUnitEntityId(), std::span<const Arche::EntityID>{ EntityIds.data(), EntityIds.size() }, FrameInput, RenderGatherResultValue };

            for (IPipelineSystem* PipelineSystem : WorkUnit.GetPipelineSystems()) {
                if (PipelineSystem == nullptr) {
                    continue;
                }

                PipelineSystem->Execute(Ctx, Dt);
            }
        }

        std::size_t PipelineExecutor::ResolveTotalWorkerCount(std::size_t WorkUnitCount) const {
            if (WorkUnitCount == 0) {
                return 0;
            }

            const std::size_t ThreadPoolWorkerCount{ mThreadPool.get_thread_count() };
            if (ThreadPoolWorkerCount == 0) {
                return 1;
            }

            return std::min(WorkUnitCount, ThreadPoolWorkerCount);
        }

        std::size_t PipelineExecutor::ResolveWorkUnitChunkSize(std::size_t WorkUnitCount, std::size_t TotalWorkerCount) const {
            if (TotalWorkerCount <= 1) {
                return WorkUnitCount;
            }

            constexpr std::size_t TargetChunkCountPerWorker{ 4 };
            constexpr std::size_t MaxChunkSize{ 16 };
            const std::size_t TargetChunkCount{ TotalWorkerCount * TargetChunkCountPerWorker };
            const std::size_t RawChunkSize{ (WorkUnitCount + TargetChunkCount - 1) / TargetChunkCount };
            return std::clamp(RawChunkSize, std::size_t{ 1 }, MaxChunkSize);
        }

        void PipelineExecutor::ExecuteWorkUnitsByDynamicPull(Arche::World& World, std::span<SceneWorkUnit> WorkUnits, const PipelineFrameInput& FrameInput, std::span<RenderContract::RenderGatherResult> RenderGatherResults, float Dt) {
            const std::size_t WorkUnitCount{ WorkUnits.size() };
            const std::size_t TotalWorkerCount{ RenderGatherResults.size() };
            const std::size_t SubmittedWorkerCount{ TotalWorkerCount > 1 ? TotalWorkerCount - 1 : 0 };
            const std::size_t WorkUnitChunkSize{ ResolveWorkUnitChunkSize(WorkUnitCount, TotalWorkerCount) };
            std::atomic<std::size_t> NextWorkUnitIndex{};
            auto WorkerFunction{ [this, &World, WorkUnits, &FrameInput, RenderGatherResults, Dt, WorkUnitCount, WorkUnitChunkSize, &NextWorkUnitIndex](std::size_t RenderGatherResultIndex) {
                RenderContract::RenderGatherResult& RenderGatherResultValue{ RenderGatherResults[RenderGatherResultIndex] };
                while (true) {
                    const std::size_t FirstWorkUnitIndex{ NextWorkUnitIndex.fetch_add(WorkUnitChunkSize, std::memory_order_relaxed) };
                    if (FirstWorkUnitIndex >= WorkUnitCount) {
                        break;
                    }

                    const std::size_t LastWorkUnitIndex{ std::min(FirstWorkUnitIndex + WorkUnitChunkSize, WorkUnitCount) };
                    for (std::size_t WorkUnitIndex{ FirstWorkUnitIndex }; WorkUnitIndex < LastWorkUnitIndex; ++WorkUnitIndex) {
                        ExecuteWorkUnit(World, WorkUnits[WorkUnitIndex], FrameInput, RenderGatherResultValue, Dt);
                    }
                }
            } };

            std::vector<std::future<void>> WorkerFutures{};
            WorkerFutures.reserve(SubmittedWorkerCount);
            for (std::size_t WorkerIndex{}; WorkerIndex < SubmittedWorkerCount; ++WorkerIndex) {
                const std::size_t RenderGatherResultIndex{ WorkerIndex + 1 };
                WorkerFutures.push_back(mThreadPool.submit_task([WorkerFunction, RenderGatherResultIndex]() {
                    WorkerFunction(RenderGatherResultIndex);
                }));
            }

            WorkerFunction(0);

            for (std::future<void>& WorkerFuture : WorkerFutures) {
                WorkerFuture.wait();
            }
        }

        void PipelineExecutor::Execute(Arche::World& World, std::span<SceneWorkUnit> WorkUnits, const PipelineFrameInput& FrameInput, float Dt) {
            const std::size_t WorkUnitCount{ WorkUnits.size() };
            if (WorkUnitCount == 0) {
                PrepareRenderGatherResults(0);
                return;
            }

            const std::size_t TotalWorkerCount{ ResolveTotalWorkerCount(WorkUnitCount) };
            PrepareRenderGatherResults(TotalWorkerCount);
            std::span<RenderContract::RenderGatherResult> RenderGatherResultSpan{ mRenderGatherResults.data(), mActiveRenderGatherResultCount };

            if (WorkUnitCount == 1) {
                ExecuteWorkUnit(World, WorkUnits[0], FrameInput, RenderGatherResultSpan[0], Dt);
                return;
            }

            ExecuteWorkUnitsByDynamicPull(World, WorkUnits, FrameInput, RenderGatherResultSpan, Dt);
        }

        std::span<const RenderContract::RenderGatherResult> PipelineExecutor::GetRenderGatherResults() const {
            return std::span<const RenderContract::RenderGatherResult>{ mRenderGatherResults.data(), mActiveRenderGatherResultCount };
        }

        void PipelineExecutor::PrepareRenderGatherResults(std::size_t RenderGatherResultCount) {
            if (mRenderGatherResults.size() < RenderGatherResultCount) {
                mRenderGatherResults.resize(RenderGatherResultCount);
            }

            mActiveRenderGatherResultCount = RenderGatherResultCount;
            for (std::size_t RenderGatherResultIndex{}; RenderGatherResultIndex < mActiveRenderGatherResultCount; ++RenderGatherResultIndex) {
                mRenderGatherResults[RenderGatherResultIndex].Clear();
            }
        }
    }
}
