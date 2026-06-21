#include "RenderContract/Writer/EnvironmentRenderWriter.h"

#include <array>
#include <stdexcept>

using namespace RenderContract;

EnvironmentRenderWriter::EnvironmentRenderWriter(RenderGatherResult& RenderGatherResultValue)
    : mRenderGatherResult{ &RenderGatherResultValue } {
}

std::vector<EnvironmentInstanceContext>& EnvironmentRenderWriter::GetEnvironmentInstanceContexts() {
    return mRenderGatherResult->GetEnvironmentInstanceContexts();
}

std::vector<EnvironmentSegmentContext>& EnvironmentRenderWriter::GetEnvironmentSegmentContexts() {
    return mRenderGatherResult->GetEnvironmentSegmentContexts();
}

std::vector<EnvironmentDrawRecord>& EnvironmentRenderWriter::GetEnvironmentDrawRecords() {
    return mRenderGatherResult->GetEnvironmentDrawRecords();
}

std::vector<EnvironmentDrawRecord>& EnvironmentRenderWriter::GetShadowEnvironmentDrawRecords(std::uint32_t CascadeIndex) {
    std::array<ShadowRenderContext, ShadowCascadeMaxCount>& ShadowRenderContexts{ mRenderGatherResult->GetShadowRenderContexts() };
    if (CascadeIndex >= ShadowRenderContexts.size()) {
        throw std::out_of_range{ "CascadeIndex" };
    }

    return ShadowRenderContexts[CascadeIndex].mEnvironmentDrawRecords;
}
