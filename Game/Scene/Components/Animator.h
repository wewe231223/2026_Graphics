#pragma once

#include <cstdint>
#include "Asset/AnimationClipResult.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    Component(Animator)
        asset::Animation* animation{ nullptr };
        std::int32_t clipIndex{ -1 };
        double counter{ 0.0 };
    EndComponent(Animator)
}
