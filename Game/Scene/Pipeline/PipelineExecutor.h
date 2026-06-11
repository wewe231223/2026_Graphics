#pragma once
#include <cstddef>
#include <span>
#include <vector>
#include "Arche/World.h"
#include "External/Include/BS_thread_pool.hpp"
#include "Game/Scene/Pipeline/RenderGatherResult.h"
#include "Game/Scene/Pipeline/SceneWorkUnit.h"

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
            std::span<const RenderGatherResult> GetRenderGatherResults() const;

        private:
            void PrepareRenderGatherResults(std::size_t RenderGatherResultCount);

        private:
            BS::thread_pool<BS::tp::none> mThreadPool{};
            std::vector<RenderGatherResult> mRenderGatherResults{};
            std::size_t mActiveRenderGatherResultCount{};
        };
    }
}
