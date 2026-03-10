#pragma once

#include <string>
#include "Game/Scene/System.h"

namespace Game {
    class PickingSystem final : public ISystem {
    public:
        PickingSystem() = default;
        ~PickingSystem() override = default;

        PickingSystem(const PickingSystem& Other) = default;
        PickingSystem& operator=(const PickingSystem& Other) = default;

        PickingSystem(PickingSystem&& Other) noexcept = default;
        PickingSystem& operator=(PickingSystem&& Other) noexcept = default;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        const std::string mName{ "PickingSystem" };
        Arche::EntityID mLastGizmoPickedEntityId{ Arche::NullEntityID };
    };
}
