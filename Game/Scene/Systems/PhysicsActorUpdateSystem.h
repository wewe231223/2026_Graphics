#pragma once

#include <string>
#include "Game/Scene/System.h"

namespace Game {
    class PhysicsActorUpdateSystem final : public ISystem {
    public:
        PhysicsActorUpdateSystem() = default;
        ~PhysicsActorUpdateSystem() override = default;
        PhysicsActorUpdateSystem(const PhysicsActorUpdateSystem& Other) = default;
        PhysicsActorUpdateSystem& operator=(const PhysicsActorUpdateSystem& Other) = default;
        PhysicsActorUpdateSystem(PhysicsActorUpdateSystem&& Other) noexcept = default;
        PhysicsActorUpdateSystem& operator=(PhysicsActorUpdateSystem&& Other) noexcept = default;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        const std::string mName{ "PhysicsActorUpdateSystem" };
    };
}
