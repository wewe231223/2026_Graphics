#pragma once
#include <string>
#include "Game/Scene/Base/System.h"

namespace Game {
    namespace Pipeline {
        class PipelineAnimationGraphSystem final : public IPipelineSystem {
        public:
            PipelineAnimationGraphSystem();
            ~PipelineAnimationGraphSystem() override;

            PipelineAnimationGraphSystem(const PipelineAnimationGraphSystem& Other);
            PipelineAnimationGraphSystem& operator=(const PipelineAnimationGraphSystem& Other);

            PipelineAnimationGraphSystem(PipelineAnimationGraphSystem&& Other) noexcept;
            PipelineAnimationGraphSystem& operator=(PipelineAnimationGraphSystem&& Other) noexcept;

        public:
            const std::string& Name() const override;
            void Execute(PipelineContext& Ctx, float Dt) override;
        };
    }
}
