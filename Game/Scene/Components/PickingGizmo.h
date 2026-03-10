#pragma once

#include <cstdint>
#include "Utility/ComponentRestraint.h"

namespace Game {
    Component(PickingGizmo)
        std::uint32_t axisIndex{ 0 };
        float directionSign{ 1.0f };
    EndComponent(PickingGizmo)
}
