#pragma once

#include <vector>

#include "RenderContract/Environment/EnvironmentDrawRecord.h"
#include "RenderContract/Model/DrawRecord.h"
#include "RenderContract/Model/ModelContext.h"
#include "RenderContract/Terrain/TerrainPatchContext.h"

namespace RenderContract {
    struct ShadowRenderContext final {
    public:
        std::vector<ModelContext> mModelContexts{};
        std::vector<TerrainPatchContext> mTerrainPatchContexts{};
        std::vector<DrawRecord> mDrawRecords{};
        std::vector<EnvironmentDrawRecord> mEnvironmentDrawRecords{};
    };
}
