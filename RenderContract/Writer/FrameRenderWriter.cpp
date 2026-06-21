#include "RenderContract/Writer/FrameRenderWriter.h"

using namespace RenderContract;

FrameRenderWriter::FrameRenderWriter(RenderFrameData& RenderFrameDataValue)
    : mRenderFrameData{ &RenderFrameDataValue } {
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
