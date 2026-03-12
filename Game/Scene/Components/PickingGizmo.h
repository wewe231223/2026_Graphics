#pragma once

#include <cstdint>
#include "Utility/ComponentRestraint.h"

namespace Game {
    Component(PickingGizmo)
        std::uint32_t axisIndex{ 0 };
    EndComponent(PickingGizmo)
}
