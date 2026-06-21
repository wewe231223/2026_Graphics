#pragma once

#include "RenderContract/Frame/CameraParameter.h"
#include "RenderContract/Frame/FrameGlobals.h"
#include "RenderContract/Frame/RenderFrameData.h"
#include "RenderContract/Frame/ShadowMappingParameter.h"

namespace RenderContract {
    class FrameRenderWriter final {
    public:
        explicit FrameRenderWriter(RenderFrameData& RenderFrameDataValue);

    public:
        void SetFrameGlobals(const FrameGlobals& FrameGlobalsValue);
        void SetCameraParameter(const CameraParameter& CameraParameterValue);
        void SetShadowMappingParameter(const ShadowMappingParameter& ShadowMappingParameterValue);

    private:
        RenderFrameData* mRenderFrameData{};
    };
}
