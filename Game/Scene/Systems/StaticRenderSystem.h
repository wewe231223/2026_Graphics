#pragma once

#include <string>
#include <unordered_map>
#include "Arche/Common.h"
#include "Game/Scene/System.h"

namespace Game {
    struct Frustum;

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
        void TraverseHierarchy(Arche::World& World, Arche::EntityID EntityId, const SimpleMath::Matrix& ParentWorld, const Frustum* CullingFrustumComponent, RFD::RenderFrameData& RenderData, const std::vector<RegisteredMaterialGroup>& MaterialGroups, Arche::EntityID PickedEntityId, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& WorldMatrices) const;
        bool IsVisibleByFrustum(Arche::World& World, Arche::EntityID EntityId, const SimpleMath::Matrix& NodeWorld, const Frustum* CullingFrustumComponent) const;

    private:
        const std::string mName{ "StaticRenderSystem" };
    };
}
