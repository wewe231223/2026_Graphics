#pragma once

#include <cstdint>

namespace RenderContract {
    struct alignas(16) MaterialTextureTableItemGpu final {
    public:
        std::uint32_t mTextureSrvDescriptorIndex{};
        std::uint32_t mPadding0{};
        std::uint32_t mPadding1{};
        std::uint32_t mPadding2{};
    };
}
