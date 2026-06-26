#pragma once

#include <memory>
#include <string>

#include "Arche/World.h"
#include "Game/Scene/Base/FrameContext.h"

namespace Game {
    class EnvironmentFoliageRuntime final {
    public:
        explicit EnvironmentFoliageRuntime(std::string ConfigPath);
        ~EnvironmentFoliageRuntime();
        EnvironmentFoliageRuntime(const EnvironmentFoliageRuntime& Other) = delete;
        EnvironmentFoliageRuntime& operator=(const EnvironmentFoliageRuntime& Other) = delete;
        EnvironmentFoliageRuntime(EnvironmentFoliageRuntime&& Other) noexcept;
        EnvironmentFoliageRuntime& operator=(EnvironmentFoliageRuntime&& Other) noexcept;

    public:
        void SetConfigPath(const std::string& ConfigPath);
        const std::string& GetConfigPath() const;
        void Update(Arche::World& World, FrameContext& Ctx, float Dt);

    private:
        class Impl;

    private:
        std::string mConfigPath{};
        std::unique_ptr<Impl> mImpl{};
    };
}
