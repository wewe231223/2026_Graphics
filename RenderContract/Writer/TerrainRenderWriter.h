#pragma once

#include <vector>

#include "RenderContract/Future/Future.h"
#include "RenderContract/Gather/RenderGatherResult.h"
#include "RenderContract/Terrain/TerrainPatchContext.h"

namespace RenderContract {
    class TerrainRenderWriter final {
    public:
        explicit TerrainRenderWriter(RenderGatherResult& RenderGatherResultValue);

    public:
        std::vector<TerrainPatchContext>& GetTerrainPatchContexts();
        void SetTerrainUploadFuture(const Future& TerrainUploadFuture);

    private:
        RenderGatherResult* mRenderGatherResult{};
    };
}
