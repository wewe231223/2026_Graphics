#pragma once
#include <span>
#include "Arche/World.h"
#include "Game/Scene/Pipeline/SceneWorkUnit.h"

namespace Game {
    namespace Pipeline {
        class PipelineExecutor final {
        public:
            PipelineExecutor();
            ~PipelineExecutor();

            PipelineExecutor(const PipelineExecutor& Other);
            PipelineExecutor& operator=(const PipelineExecutor& Other);

            PipelineExecutor(PipelineExecutor&& Other) noexcept;
            PipelineExecutor& operator=(PipelineExecutor&& Other) noexcept;

        public:
            void Execute(Arche::World& World, std::span<SceneWorkUnit> WorkUnits, float Dt);
        };
    }
}
