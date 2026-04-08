#pragma once

#include <string>

#include "Game/Scene/System.h"

namespace Game {
    class TransformWorldSystem final : public ISystem {
    public:
        TransformWorldSystem() = default;
        ~TransformWorldSystem() override = default;
        TransformWorldSystem(const TransformWorldSystem& Other) = default;
        TransformWorldSystem& operator=(const TransformWorldSystem& Other) = default;
        TransformWorldSystem(TransformWorldSystem&& Other) noexcept = default;
        TransformWorldSystem& operator=(TransformWorldSystem&& Other) noexcept = default;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        const std::string mName{ "TransformWorldSystem" };
    };
}
