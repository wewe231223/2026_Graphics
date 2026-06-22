#include "RenderContract/Writer/TerrainRenderWriter.h"

using namespace RenderContract;

TerrainRenderWriter::TerrainRenderWriter(RenderGatherResult& RenderGatherResultValue)
    : mRenderGatherResult{ &RenderGatherResultValue } {
}

std::vector<ModelContext>& TerrainRenderWriter::GetModelContexts() {
    return mRenderGatherResult->GetModelContexts();
}

std::vector<BoundingBoxContext>& TerrainRenderWriter::GetBoundingBoxContexts() {
    return mRenderGatherResult->GetBoundingBoxContexts();
}

std::vector<TerrainPatchContext>& TerrainRenderWriter::GetTerrainPatchContexts() {
    return mRenderGatherResult->GetTerrainPatchContexts();
}

std::vector<DrawRecord>& TerrainRenderWriter::GetDrawRecords() {
    return mRenderGatherResult->GetDrawRecords();
}

std::array<ShadowRenderContext, ShadowCascadeMaxCount>& TerrainRenderWriter::GetShadowRenderContexts() {
    return mRenderGatherResult->GetShadowRenderContexts();
}

void TerrainRenderWriter::SetTerrainUploadFuture(const Future& TerrainUploadFuture) {
    mRenderGatherResult->SetTerrainUploadFuture(TerrainUploadFuture);
}
