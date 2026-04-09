#pragma once

#include <string>
#include "Game/Scene/System.h"

namespace Game {
    class BoundingBoxUpdateSystem final : public ISystem {
    public:
        BoundingBoxUpdateSystem() = default;
        ~BoundingBoxUpdateSystem() override = default;
        BoundingBoxUpdateSystem(const BoundingBoxUpdateSystem& Other) = default;
        BoundingBoxUpdateSystem& operator=(const BoundingBoxUpdateSystem& Other) = default;
        BoundingBoxUpdateSystem(BoundingBoxUpdateSystem&& Other) noexcept = default;
        BoundingBoxUpdateSystem& operator=(BoundingBoxUpdateSystem&& Other) noexcept = default;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        const std::string mName{ "BoundingBoxUpdateSystem" };
    };
}
