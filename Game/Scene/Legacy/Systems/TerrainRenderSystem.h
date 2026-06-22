#pragma once

#include <string>
#include "Arche/Common.h"
#include "Game/Scene/Base/SynchronousSystem.h"

namespace Game {
    class TerrainRenderSystem final : public ISystem {
    public:
        TerrainRenderSystem();
        ~TerrainRenderSystem() override;
        TerrainRenderSystem(const TerrainRenderSystem& Other);
        TerrainRenderSystem& operator=(const TerrainRenderSystem& Other);
        TerrainRenderSystem(TerrainRenderSystem&& Other) noexcept;
        TerrainRenderSystem& operator=(TerrainRenderSystem&& Other) noexcept;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        const std::string mName{ "TerrainRenderSystem" };
    };
}
