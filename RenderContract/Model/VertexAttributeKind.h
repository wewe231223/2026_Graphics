#pragma once

#include <cstdint>

namespace RenderContract {
    enum class VertexAttributeKind : std::uint32_t {
        Position,
        Normal,
        TexCoord0,
        TexCoord1,
        TexCoord2,
        TexCoord3,
        Color,
        Tangent,
        Bitangent,
        BoneIndices,
        BoneWeights
    };
}
