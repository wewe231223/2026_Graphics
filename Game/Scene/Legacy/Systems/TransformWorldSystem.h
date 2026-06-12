#pragma once

#include <string>
#include "Game/Scene/Base/SynchronousSystem.h"

namespace Game {
    class TransformWorldSystem final : public ISystem {
    public:
        TransformWorldSystem();
        ~TransformWorldSystem() override;
        TransformWorldSystem(const TransformWorldSystem& Other);
        TransformWorldSystem& operator=(const TransformWorldSystem& Other);
        TransformWorldSystem(TransformWorldSystem&& Other) noexcept;
        TransformWorldSystem& operator=(TransformWorldSystem&& Other) noexcept;

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
