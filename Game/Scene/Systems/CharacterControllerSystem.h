#pragma once

#include <string>

#include "Game/Scene/Base/SynchronousSystem.h"

namespace Game {
    class CharacterControllerSystem final : public ISystem {
    public:
        CharacterControllerSystem() = default;
        ~CharacterControllerSystem() override = default;
        CharacterControllerSystem(const CharacterControllerSystem& Other) = default;
        CharacterControllerSystem& operator=(const CharacterControllerSystem& Other) = default;
        CharacterControllerSystem(CharacterControllerSystem&& Other) noexcept = default;
        CharacterControllerSystem& operator=(CharacterControllerSystem&& Other) noexcept = default;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        const std::string mName{ "CharacterControllerSystem" };
    };
}
