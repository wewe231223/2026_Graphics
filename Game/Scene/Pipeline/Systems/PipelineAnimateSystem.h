#pragma once
#include <string>
#include "Game/Scene/Pipeline/PipelineSystem.h"

namespace Game {
    namespace Pipeline {
        class PipelineAnimateSystem final : public IPipelineSystem {
        public:
            PipelineAnimateSystem();
            ~PipelineAnimateSystem() override;

            PipelineAnimateSystem(const PipelineAnimateSystem& Other);
            PipelineAnimateSystem& operator=(const PipelineAnimateSystem& Other);

            PipelineAnimateSystem(PipelineAnimateSystem&& Other) noexcept;
            PipelineAnimateSystem& operator=(PipelineAnimateSystem&& Other) noexcept;

        public:
            const std::string& Name() const override;
            void Execute(PipelineContext& Ctx, float Dt) override;
        };
    }
}
