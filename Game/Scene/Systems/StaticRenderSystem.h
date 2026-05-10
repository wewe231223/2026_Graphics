#pragma once

#include <string>
#include "Arche/Common.h"
#include "Game/Scene/System.h"

namespace Game {
    struct Frustum;

    class StaticRenderSystem final : public ISystem {
    public:
        StaticRenderSystem() = default;
        ~StaticRenderSystem() override = default;
        StaticRenderSystem(const StaticRenderSystem& Other) = default;
        StaticRenderSystem& operator=(const StaticRenderSystem& Other) = default;
        StaticRenderSystem(StaticRenderSystem&& Other) noexcept = default;
        StaticRenderSystem& operator=(StaticRenderSystem&& Other) noexcept = default;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        bool IsVisibleByFrustum(Arche::World& World, Arche::EntityID EntityId, const Frustum* CullingFrustumComponent) const;
        bool IsVisibleByShadowBox(Arche::World& World, Arche::EntityID EntityId, const DirectX::BoundingOrientedBox& CullingBox) const;

    private:
        const std::string mName{ "StaticRenderSystem" };
    };
}
