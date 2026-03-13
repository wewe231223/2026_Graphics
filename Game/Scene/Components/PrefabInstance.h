#pragma once

#include <cstdint>
#include "Utility/ComponentRestraint.h"

namespace Game {
    Component(PrefabInstance)
        std::uint64_t PrefabId{};
    EndComponent(PrefabInstance)
}
