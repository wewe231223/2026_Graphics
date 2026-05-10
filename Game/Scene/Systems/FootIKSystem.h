#pragma once

#include <memory>
#include <string>
#include "Game/Scene/System.h"

namespace Game {
    class IFootIKSolver;

    class FootIKSystem final : public ISystem {
    public:
        FootIKSystem();
        ~FootIKSystem() override;
        FootIKSystem(const FootIKSystem& Other);
        FootIKSystem& operator=(const FootIKSystem& Other);
        FootIKSystem(FootIKSystem&& Other) noexcept;
        FootIKSystem& operator=(FootIKSystem&& Other) noexcept;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        std::string mName{};
        std::unique_ptr<IFootIKSolver> mFootIKSolver{};
    };
}
