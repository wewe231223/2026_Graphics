#pragma once

#include <cstdint>
#include "Game/Environment/EnvironmentObjectTypes.h"
#include "Game/Scene/Base/RenderGatherResult.h"

namespace Game {
    struct EnvironmentObjectRenderBuildOptions final {
    public:
        std::uint32_t mLodLevel{};
        std::uint32_t mShadowCascadeMask{ 0xffffffffu };
        bool mEnableMainPass{ true };
        bool mEnableShadowPass{ true };
    };

    void AppendEnvironmentObjectCellRenderData(const EnvironmentObjectCell& Cell, const EnvironmentObjectRenderBuildOptions& Options, Pipeline::RenderGatherResult& OutRenderGatherResult);
}
