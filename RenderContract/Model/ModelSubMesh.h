#pragma once

#include <cstddef>

namespace RenderContract {
    struct ModelSubMesh final {
    public:
        std::size_t mIndexOffset{};
        std::size_t mIndexCount{};
        std::size_t mMaterialGroupItemIndex{};
    };
}
