#pragma once

#include <string>
#include "Arche/Common.h"
#include "Game/Scene/System.h"

namespace Game {
    class StaticRenderSystem final : public ISystem {
    public:
        StaticRenderSystem() = default;
        ~StaticRenderSystem() override = default;

        StaticRenderSystem(const StaticRenderSystem&) = default;
        StaticRenderSystem& operator=(const StaticRenderSystem&) = default;

        StaticRenderSystem(StaticRenderSystem&&) noexcept = default;
        StaticRenderSystem& operator=(StaticRenderSystem&&) noexcept = default;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        void TraverseHierarchy(Arche::World& World, Arche::EntityID EntityId, const SimpleMath::Matrix& ParentWorld, RFD::RenderFrameData& RenderData, const std::vector<RegisteredMaterialGroup>& MaterialGroups) const;

    private:
        const std::string mName{ "StaticRenderSystem" };
    };
}
