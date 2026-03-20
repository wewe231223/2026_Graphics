#pragma once

#include <cstdint>

#include "Game/Model/Model.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    Component(Bone)
        Model* model{ nullptr };
        std::uint32_t nodeIndex{ 0 };
    EndComponent(Bone)
}
