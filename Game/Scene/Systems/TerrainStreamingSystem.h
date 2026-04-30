#pragma once

#include <string>

#include "Game/Scene/System.h"

namespace Game {
    class TerrainStreamingSystem final : public ISystem {
    public:
        TerrainStreamingSystem();
        ~TerrainStreamingSystem() override;
        TerrainStreamingSystem(const TerrainStreamingSystem& Other);
        TerrainStreamingSystem& operator=(const TerrainStreamingSystem& Other);
        TerrainStreamingSystem(TerrainStreamingSystem&& Other) noexcept;
        TerrainStreamingSystem& operator=(TerrainStreamingSystem&& Other) noexcept;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        const std::string mName{ "TerrainStreamingSystem" };
    };
}
