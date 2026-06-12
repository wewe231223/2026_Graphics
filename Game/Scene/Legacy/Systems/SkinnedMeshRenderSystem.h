#pragma once

#include <string>
#include "Game/Scene/Base/SynchronousSystem.h"

namespace Game {
    class SkinnedMeshRenderSystem final : public ISystem {
    public:
        SkinnedMeshRenderSystem();
        ~SkinnedMeshRenderSystem() override;
        SkinnedMeshRenderSystem(const SkinnedMeshRenderSystem& Other) = delete;
        SkinnedMeshRenderSystem& operator=(const SkinnedMeshRenderSystem& Other) = delete;
        SkinnedMeshRenderSystem(SkinnedMeshRenderSystem&& Other) noexcept = delete;
        SkinnedMeshRenderSystem& operator=(SkinnedMeshRenderSystem&& Other) noexcept = delete;

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
