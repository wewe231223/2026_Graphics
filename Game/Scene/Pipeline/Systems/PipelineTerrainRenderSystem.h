#pragma once
#include <string>
#include "Game/Scene/Pipeline/PipelineSystem.h"

namespace Game {
    namespace Pipeline {
        class PipelineTerrainRenderSystem final : public IPipelineSystem {
        public:
            PipelineTerrainRenderSystem();
            ~PipelineTerrainRenderSystem() override;

            PipelineTerrainRenderSystem(const PipelineTerrainRenderSystem& Other);
            PipelineTerrainRenderSystem& operator=(const PipelineTerrainRenderSystem& Other);

            PipelineTerrainRenderSystem(PipelineTerrainRenderSystem&& Other) noexcept;
            PipelineTerrainRenderSystem& operator=(PipelineTerrainRenderSystem&& Other) noexcept;

        public:
            const std::string& Name() const override;
            void Execute(PipelineContext& Ctx, float Dt) override;
        };
    }
}
