#pragma once
#include <string>
#include "Game/Scene/Base/System.h"

namespace Game {
    namespace Pipeline {
        class PipelineSkinnedRenderSystem final : public IPipelineSystem {
        public:
            PipelineSkinnedRenderSystem();
            ~PipelineSkinnedRenderSystem() override;

            PipelineSkinnedRenderSystem(const PipelineSkinnedRenderSystem& Other);
            PipelineSkinnedRenderSystem& operator=(const PipelineSkinnedRenderSystem& Other);

            PipelineSkinnedRenderSystem(PipelineSkinnedRenderSystem&& Other) noexcept;
            PipelineSkinnedRenderSystem& operator=(PipelineSkinnedRenderSystem&& Other) noexcept;

        public:
            const std::string& Name() const override;
            void Execute(PipelineContext& Ctx, float Dt) override;
        };
    }
}
