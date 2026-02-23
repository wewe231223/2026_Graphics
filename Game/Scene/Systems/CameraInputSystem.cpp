#include "CameraInputSystem.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/Intents/CameraIntent.h"

namespace Game {
    const std::string& Game::CameraInputSystem::Name() const {
        return mName; 
    }

    Phase Game::CameraInputSystem::GetPhase() const {
        return Phase::PreUpdate;
    }

    std::span<const ComponentAccess> Game::CameraInputSystem::ComponentAccesses() const {
		std::array<ComponentAccess, 2> accesses{ 
            { { typeid(CameraIntent), Access::Write }, 
            { typeid(Camera), Access::Read } 
        } };

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