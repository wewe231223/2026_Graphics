#pragma once

#include <string>
#include "Game/Scene/ParallelSystemBase.h"

namespace Game {
    class StaticBoundingBoxUpdateSystem final : public ParallelSystemBase {
    public:
        StaticBoundingBoxUpdateSystem();
        ~StaticBoundingBoxUpdateSystem() override;
        StaticBoundingBoxUpdateSystem(const StaticBoundingBoxUpdateSystem& Other) = delete;
        StaticBoundingBoxUpdateSystem& operator=(const StaticBoundingBoxUpdateSystem& Other) = delete;
        StaticBoundingBoxUpdateSystem(StaticBoundingBoxUpdateSystem&& Other) noexcept = delete;
        StaticBoundingBoxUpdateSystem& operator=(StaticBoundingBoxUpdateSystem&& Other) noexcept = delete;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        const std::string mName{ "StaticBoundingBoxUpdateSystem" };
    };
}
