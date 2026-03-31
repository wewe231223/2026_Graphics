#pragma once

#include <string>
#include "Game/Scene/System.h"

namespace Game {
    class AnimateSystem final : public ISystem {
    public:
        AnimateSystem() = default;
        ~AnimateSystem() override = default;
        AnimateSystem(const AnimateSystem& Other) = default;
        AnimateSystem& operator=(const AnimateSystem& Other) = default;
        AnimateSystem(AnimateSystem&& Other) noexcept = default;
        AnimateSystem& operator=(AnimateSystem&& Other) noexcept = default;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        const std::string mName{ "AnimateSystem" };
    };
}
