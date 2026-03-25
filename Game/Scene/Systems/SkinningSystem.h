#pragma once

#include <string>

#include "Game/Scene/System.h"

namespace Game {
    class SkinningSystem final : public ISystem {
    public:
        SkinningSystem() = default;
        ~SkinningSystem() override = default;
        SkinningSystem(const SkinningSystem& Other) = default;
        SkinningSystem& operator=(const SkinningSystem& Other) = default;
        SkinningSystem(SkinningSystem&& Other) noexcept = default;
        SkinningSystem& operator=(SkinningSystem&& Other) noexcept = default;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        const std::string mName{ "SkinningSystem" };
    };
}
