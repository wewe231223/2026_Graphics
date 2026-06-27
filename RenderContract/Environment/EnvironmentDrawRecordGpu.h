#pragma once

#include <cstdint>

namespace RenderContract {
    struct alignas(16) EnvironmentDrawRecordGpu final {
    public:
        std::uint32_t mInstanceOffset{};
        std::uint32_t mInstanceCount{};
        std::uint32_t mSegmentContextIndex{};
        std::uint32_t mMaterialIndex{};
        std::uint32_t mFlags{};
        std::uint32_t mVisibleInstanceOffset{};
        std::uint32_t mGpuDrivenFlags{};
        std::uint32_t mIndexCountPerInstance{};
        std::uint32_t mStartIndexLocation{};
        std::int32_t mBaseVertexLocation{};
        std::uint32_t mPadding2{};
        std::uint32_t mPadding3{};
    };

    static_assert(sizeof(EnvironmentDrawRecordGpu) == 48u);
}
