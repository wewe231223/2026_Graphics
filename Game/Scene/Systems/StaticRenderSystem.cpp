#include "StaticRenderSystem.h"


namespace Game {
    const std::string& Game::StaticRenderSystem::Name() const {
        return mName; 
    }

    Phase Game::StaticRenderSystem::GetPhase() const {
        return Phase::Render;
    }

    std::span<const ComponentAccess> Game::StaticRenderSystem::ComponentAccesses() const {
        return std::span<const ComponentAccess>();
    }

    std::span<const ResourceAccess> Game::StaticRenderSystem::ResourceAccesses() const
    {
        return std::span<const ResourceAccess>();
    }

    void Game::StaticRenderSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt)
    {
    }
}

