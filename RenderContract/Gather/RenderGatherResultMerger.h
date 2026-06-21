#pragma once

#include <span>

#include "RenderContract/Frame/RenderFrameData.h"
#include "RenderContract/Gather/RenderGatherResult.h"

namespace RenderContract {
    class RenderGatherResultMerger final {
    public:
        static void Merge(std::span<const RenderGatherResult> RenderGatherResults, RenderFrameData& OutRenderFrameData);
    };
}
