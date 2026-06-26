#include "RenderContract/Writer/FrameRenderWriter.h"

using namespace RenderContract;

FrameRenderWriter::FrameRenderWriter(RenderFrameData& RenderFrameDataValue)
    : mRenderFrameData{ &RenderFrameDataValue } {
}

void FrameRenderWriter::BeginFrame() {
    mRenderFrameData->mModelContexts.clear();
    mRenderFrameData->mBoundingBoxContexts.clear();
    mRenderFrameData->mDebugGeometryContexts.clear();
    mRenderFrameData->mTerrainPatchContexts.clear();
    mRenderFrameData->mDrawRecords.clear();
    mRenderFrameData->mBonePalette.clear();
    mRenderFrameData->mEnvironmentInstanceContexts.clear();
    mRenderFrameData->mEnvironmentSegmentContexts.clear();
    mRenderFrameData->mEnvironmentDrawRecords.clear();
    mRenderFrameData->mEnvironmentGpuDrivenFrame = EnvironmentGpuDrivenFrameData{};
    mRenderFrameData->mEnvironmentRuntime = nullptr;
    mRenderFrameData->mTerrainUploadFuture = Future{};
    mRenderFrameData->mHasTerrainUploadFuture = false;
    mRenderFrameData->mFrameGlobals.mFlags = 0u;

    for (ShadowRenderContext& ShadowRenderContext : mRenderFrameData->mShadowRenderContexts) {
        ShadowRenderContext.mModelContexts.clear();
        ShadowRenderContext.mTerrainPatchContexts.clear();
        ShadowRenderContext.mDrawRecords.clear();
        ShadowRenderContext.mEnvironmentDrawRecords.clear();
    }
}

void FrameRenderWriter::SetFrameGlobals(const FrameGlobals& FrameGlobalsValue) {
    mRenderFrameData->mFrameGlobals = FrameGlobalsValue;
}

void FrameRenderWriter::SetCameraParameter(const CameraParameter& CameraParameterValue) {
    mRenderFrameData->mMainCamera = CameraParameterValue;
}

void FrameRenderWriter::SetShadowMappingParameter(const ShadowMappingParameter& ShadowMappingParameterValue) {
    mRenderFrameData->mShadowMappingParameter = ShadowMappingParameterValue;
}
