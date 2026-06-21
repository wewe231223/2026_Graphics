#pragma once
#include <cstddef>
#include <span>
#include <vector>
#include "Arche/World.h"
#include "External/Include/BS_thread_pool.hpp"
#include "RenderContract/Gather/RenderGatherResult.h"
#include "Game/Scene/Base/SceneWorkUnit.h"

namespace Game {
    namespace Pipeline {
        struct PipelineFrameInput;

        class PipelineExecutor final {
        public:
            PipelineExecutor();
            ~PipelineExecutor();

            PipelineExecutor(const PipelineExecutor& Other);
            PipelineExecutor& operator=(const PipelineExecutor& Other);

            PipelineExecutor(PipelineExecutor&& Other) noexcept;
            PipelineExecutor& operator=(PipelineExecutor&& Other) noexcept;

        public:
            void Execute(Arche::World& World, std::span<SceneWorkUnit> WorkUnits, const PipelineFrameInput& FrameInput, float Dt);
            std::span<const RenderContract::RenderGatherResult> GetRenderGatherResults() const;

        private:
            void ExecuteWorkUnit(Arche::World& World, SceneWorkUnit& WorkUnit, const PipelineFrameInput& FrameInput, RenderContract::RenderGatherResult& RenderGatherResultValue, float Dt) const;
            std::size_t ResolveTotalWorkerCount(std::size_t WorkUnitCount) const;
            std::size_t ResolveWorkUnitChunkSize(std::size_t WorkUnitCount, std::size_t TotalWorkerCount) const;
            void ExecuteWorkUnitsByDynamicPull(Arche::World& World, std::span<SceneWorkUnit> WorkUnits, const PipelineFrameInput& FrameInput, std::span<RenderContract::RenderGatherResult> RenderGatherResults, float Dt);

            void PrepareRenderGatherResults(std::size_t RenderGatherResultCount);

        private:
            BS::thread_pool<BS::tp::none> mThreadPool{};
            std::vector<RenderContract::RenderGatherResult> mRenderGatherResults{};
            std::size_t mActiveRenderGatherResultCount{};
        };
    }
}
