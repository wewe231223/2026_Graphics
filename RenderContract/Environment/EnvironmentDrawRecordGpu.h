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
        std::uint32_t mPadding0{};
        std::uint32_t mPadding1{};
        std::uint32_t mPadding2{};
    };
}
