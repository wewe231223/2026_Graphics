#pragma once

#include <memory>
#include <string>

#include "Game/Scene/Base/SynchronousSystem.h"

namespace Game {
    class ProceduralFoliageRuntime;

    class ProceduralFoliageSystem final : public ISystem {
    public:
        ProceduralFoliageSystem();
        ~ProceduralFoliageSystem() override;
        ProceduralFoliageSystem(const ProceduralFoliageSystem& Other);
        ProceduralFoliageSystem& operator=(const ProceduralFoliageSystem& Other);
        ProceduralFoliageSystem(ProceduralFoliageSystem&& Other) noexcept;
        ProceduralFoliageSystem& operator=(ProceduralFoliageSystem&& Other) noexcept;

    public:
        void SetConfigPath(const std::string& ConfigPath);
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        std::string mName{};
        std::string mConfigPath{};
        std::unique_ptr<ProceduralFoliageRuntime> mRuntime{};
    };
}
