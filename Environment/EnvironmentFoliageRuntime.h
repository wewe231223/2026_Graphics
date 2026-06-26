#pragma once

#include <memory>
#include <string>

#include <DirectXTK12/SimpleMath.h>

#include "Arche/World.h"
#include "Environment/EnvironmentGpuPlacementData.h"
#include "Game/Scene/Base/FrameContext.h"

namespace RenderContract {
    struct RenderFrameData;
}

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
        void Update(Arche::World& World, FrameContext& Ctx, float Dt, bool IsGpuDrivenRenderEnabled);
        bool BuildGpuDrivenRenderData(const DirectX::SimpleMath::Vector3& FocusPosition, RenderContract::RenderFrameData& RenderData, EnvironmentGpuPlacementFrameData& OutFrameData) const;

    private:
        class Impl;

    private:
        std::string mConfigPath{};
        std::unique_ptr<Impl> mImpl{};
    };
}
