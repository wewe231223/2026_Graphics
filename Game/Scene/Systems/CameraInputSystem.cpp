#include "CameraInputSystem.h"

namespace Game {
    const std::string& Game::CameraInputSystem::Name() const {
        return mName; 
    }

    // 내일 여기부터.. 
    Phase Game::CameraInputSystem::GetPhase() const
    {
        return Phase();
    }

    std::span<const ComponentAccess> Game::CameraInputSystem::ComponentAccesses() const
    {
        return std::span<const ComponentAccess>();
    }

    std::span<const ResourceAccess> Game::CameraInputSystem::ResourceAccesses() const
    {
        return std::span<const ResourceAccess>();
    }

    void Game::CameraInputSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt)
    {
    }

    void Game::CameraInputSystem::ProcessKeyboard(CameraIntent& intent)
    {
    }

    void Game::CameraInputSystem::ProcessMouse(CameraIntent& intent)
    {
    }
} // namespace Game