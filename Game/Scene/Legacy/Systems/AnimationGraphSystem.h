#pragma once

#include <string>
#include "Game/Scene/Base/SynchronousSystem.h"

namespace Game {
    class AnimationGraphSystem final : public ISystem {
    public:
        AnimationGraphSystem() = default;
        ~AnimationGraphSystem() override = default;
        AnimationGraphSystem(const AnimationGraphSystem& Other) = default;
        AnimationGraphSystem& operator=(const AnimationGraphSystem& Other) = default;
        AnimationGraphSystem(AnimationGraphSystem&& Other) noexcept = default;
        AnimationGraphSystem& operator=(AnimationGraphSystem&& Other) noexcept = default;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        const std::string mName{ "AnimationGraphSystem" };
    };
}
