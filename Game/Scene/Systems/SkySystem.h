#pragma once

#include <string>
#include "Game/Scene/System.h"

namespace Game {
    class SkySystem final : public ISystem {
    public:
        SkySystem() = default;
        ~SkySystem() override = default;
        SkySystem(const SkySystem& Other) = default;
        SkySystem& operator=(const SkySystem& Other) = default;
        SkySystem(SkySystem&& Other) noexcept = default;
        SkySystem& operator=(SkySystem&& Other) noexcept = default;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        const std::string mName{ "SkySystem" };
    };
}
