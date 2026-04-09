#pragma once

#include <string>
#include "Game/Scene/System.h"

namespace Game {
    class TransformWorldBuildSystem final : public ISystem {
    public:
        TransformWorldBuildSystem() = default;
        ~TransformWorldBuildSystem() override = default;
        TransformWorldBuildSystem(const TransformWorldBuildSystem& Other) = default;
        TransformWorldBuildSystem& operator=(const TransformWorldBuildSystem& Other) = default;
        TransformWorldBuildSystem(TransformWorldBuildSystem&& Other) noexcept = default;
        TransformWorldBuildSystem& operator=(TransformWorldBuildSystem&& Other) noexcept = default;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        const std::string mName{ "TransformWorldBuildSystem" };
    };
}
