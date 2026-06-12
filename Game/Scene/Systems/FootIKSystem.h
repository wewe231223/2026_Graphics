#pragma once
#include <memory>
#include <string>
#include "Game/Scene/IK/FootIKSolver.h"
#include "Game/Scene/Base/System.h"

namespace Game {
    namespace Pipeline {
        class PipelineFootIKSystem final : public IPipelineSystem {
        public:
            PipelineFootIKSystem();
            ~PipelineFootIKSystem() override;

            PipelineFootIKSystem(const PipelineFootIKSystem& Other);
            PipelineFootIKSystem& operator=(const PipelineFootIKSystem& Other);

            PipelineFootIKSystem(PipelineFootIKSystem&& Other) noexcept;
            PipelineFootIKSystem& operator=(PipelineFootIKSystem&& Other) noexcept;

        public:
            const std::string& Name() const override;
            void Execute(PipelineContext& Ctx, float Dt) override;

        private:
            std::unique_ptr<IFootIKSolver> mFootIKSolver{};
        };
    }
}
