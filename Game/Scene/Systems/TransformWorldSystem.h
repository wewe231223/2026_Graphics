#pragma once
#include <string>
#include "Game/Scene/Base/System.h"

namespace Game {
    namespace Pipeline {
        class PipelineTransformWorldSystem final : public IPipelineSystem {
        public:
            PipelineTransformWorldSystem();
            ~PipelineTransformWorldSystem() override;

            PipelineTransformWorldSystem(const PipelineTransformWorldSystem& Other);
            PipelineTransformWorldSystem& operator=(const PipelineTransformWorldSystem& Other);

            PipelineTransformWorldSystem(PipelineTransformWorldSystem&& Other) noexcept;
            PipelineTransformWorldSystem& operator=(PipelineTransformWorldSystem&& Other) noexcept;

        public:
            const std::string& Name() const override;
            void Execute(PipelineContext& Ctx, float Dt) override;
        };
    }
}
