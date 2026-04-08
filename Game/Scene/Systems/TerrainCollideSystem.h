#pragma once

#include <string>
#include "Game/Scene/System.h"

namespace Game {
    class TerrainCollideSystem final : public ISystem {
    public:
        TerrainCollideSystem();
        ~TerrainCollideSystem() override;
        TerrainCollideSystem(const TerrainCollideSystem& Other);
        TerrainCollideSystem& operator=(const TerrainCollideSystem& Other);
        TerrainCollideSystem(TerrainCollideSystem&& Other) noexcept;
        TerrainCollideSystem& operator=(TerrainCollideSystem&& Other) noexcept;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        std::string mName{};
    };
}
