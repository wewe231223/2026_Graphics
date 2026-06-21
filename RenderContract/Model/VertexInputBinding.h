#pragma once

#include <cstdint>

#include "RenderContract/Model/VertexAttributeKind.h"

namespace RenderContract {
    struct VertexInputBinding final {
    public:
        VertexAttributeKind mKind{};
        std::uint32_t mInputSlot{};
    };
}
