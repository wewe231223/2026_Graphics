#pragma once

#include <cstdint>
#include "Game/Model/Model.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    Component(StaticMeshRenderer)
        Model* model{ nullptr };
        std::uint32_t nodeIndex{ 0 };
        std::uint32_t materialGroupIndex{ 0 };
        bool active{ true };
    EndComponent(StaticMeshRenderer)
}
