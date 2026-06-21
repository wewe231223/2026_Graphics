#pragma once

#include <cstdint>

#include "Asset/Common.h"

#include "RenderContract/Material/MaterialFieldGpu.h"

namespace RenderContract {
    struct alignas(16) MaterialGpu final {
    public:
        static constexpr std::uint32_t FieldCount{ asset::MaterialTypeCount };
        MaterialFieldGpu mFields[FieldCount]{};
    };
}
