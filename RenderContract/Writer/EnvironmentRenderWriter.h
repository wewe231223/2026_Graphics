#pragma once

#include <cstdint>
#include <vector>

#include "RenderContract/Environment/EnvironmentDrawRecord.h"
#include "RenderContract/Environment/EnvironmentInstanceContext.h"
#include "RenderContract/Environment/EnvironmentSegmentContext.h"
#include "RenderContract/Gather/RenderGatherResult.h"

namespace RenderContract {
    class EnvironmentRenderWriter final {
    public:
        explicit EnvironmentRenderWriter(RenderGatherResult& RenderGatherResultValue);

    public:
        std::vector<EnvironmentInstanceContext>& GetEnvironmentInstanceContexts();
        std::vector<EnvironmentSegmentContext>& GetEnvironmentSegmentContexts();
        std::vector<EnvironmentDrawRecord>& GetEnvironmentDrawRecords();
        std::vector<EnvironmentDrawRecord>& GetShadowEnvironmentDrawRecords(std::uint32_t CascadeIndex);

    private:
        RenderGatherResult* mRenderGatherResult{};
    };
}
