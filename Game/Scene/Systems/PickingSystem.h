#pragma once

#include <cstdint>
#include <string>
#include "Game/Scene/System.h"
#include "Utility/SimpleMathWrapper.h"

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
        bool mIsGizmoDragging{ false };
        Arche::EntityID mDraggingTargetEntityId{ Arche::NullEntityID };
        float mDraggingStartAxisParameter{ 0.0f };
        SimpleMath::Vector3 mDraggingTargetStartLocalPosition{};
        SimpleMath::Vector3 mDraggingAxisOriginParentSpace{};
        SimpleMath::Vector3 mDraggingAxisDirectionParentSpace{};
        SimpleMath::Matrix mDraggingParentWorldInverseMatrix{ SimpleMath::Matrix::Identity };
    };
}
