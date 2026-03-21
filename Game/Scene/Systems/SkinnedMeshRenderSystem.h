#pragma once

#include <string>
#include "Game/Scene/System.h"

namespace Game {
    class SkinnedMeshRenderSystem final : public ISystem {
    public:
        SkinnedMeshRenderSystem() = default;
        ~SkinnedMeshRenderSystem() override = default;
        SkinnedMeshRenderSystem(const SkinnedMeshRenderSystem& Other) = default;
        SkinnedMeshRenderSystem& operator=(const SkinnedMeshRenderSystem& Other) = default;
        SkinnedMeshRenderSystem(SkinnedMeshRenderSystem&& Other) noexcept = default;
        SkinnedMeshRenderSystem& operator=(SkinnedMeshRenderSystem&& Other) noexcept = default;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        const std::string mName{ "SkinnedMeshRenderSystem" };
    };
}
