#include "RenderContract/Writer/TerrainRenderWriter.h"

using namespace RenderContract;

TerrainRenderWriter::TerrainRenderWriter(RenderGatherResult& RenderGatherResultValue)
    : mRenderGatherResult{ &RenderGatherResultValue } {
}

std::vector<TerrainPatchContext>& TerrainRenderWriter::GetTerrainPatchContexts() {
    return mRenderGatherResult->GetTerrainPatchContexts();
}

void TerrainRenderWriter::SetTerrainUploadFuture(const Future& TerrainUploadFuture) {
    mRenderGatherResult->SetTerrainUploadFuture(TerrainUploadFuture);
}
