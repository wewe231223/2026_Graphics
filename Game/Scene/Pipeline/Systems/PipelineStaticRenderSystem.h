#pragma once
#include <string>
#include "Game/Scene/Pipeline/PipelineSystem.h"

namespace Game {
    namespace Pipeline {
        class PipelineStaticRenderSystem final : public IPipelineSystem {
        public:
            PipelineStaticRenderSystem();
            ~PipelineStaticRenderSystem() override;

            PipelineStaticRenderSystem(const PipelineStaticRenderSystem& Other);
            PipelineStaticRenderSystem& operator=(const PipelineStaticRenderSystem& Other);

            PipelineStaticRenderSystem(PipelineStaticRenderSystem&& Other) noexcept;
            PipelineStaticRenderSystem& operator=(PipelineStaticRenderSystem&& Other) noexcept;

        public:
            const std::string& Name() const override;
            void Execute(PipelineContext& Ctx, float Dt) override;
        };
    }
}
