#pragma once

#include <cstdint>

namespace RenderContract {
    enum class PipelineOption : std::uint32_t {
        None,
        DepthAlphaCutoff = 1u << 0
    };
}
