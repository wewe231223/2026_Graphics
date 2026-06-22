#pragma once

#include <array>
#include <vector>

#include "RenderContract/Future/Future.h"
#include "RenderContract/Gather/RenderGatherResult.h"
#include "RenderContract/Terrain/TerrainPatchContext.h"

namespace RenderContract {
    class TerrainRenderWriter final {
    public:
        explicit TerrainRenderWriter(RenderGatherResult& RenderGatherResultValue);

    public:
        std::vector<ModelContext>& GetModelContexts();
        std::vector<BoundingBoxContext>& GetBoundingBoxContexts();
        std::vector<TerrainPatchContext>& GetTerrainPatchContexts();
        std::vector<DrawRecord>& GetDrawRecords();
        std::array<ShadowRenderContext, ShadowCascadeMaxCount>& GetShadowRenderContexts();

        void SetTerrainUploadFuture(const Future& TerrainUploadFuture);

    private:
        RenderGatherResult* mRenderGatherResult{};
    };
}
