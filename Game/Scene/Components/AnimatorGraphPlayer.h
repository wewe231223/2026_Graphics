#pragma once

#include <cstdint>
#include "Utility/ComponentRestraint.h"

namespace Game {
    Component(AnimatorGraphPlayer)
        std::int32_t CurrentNodeIndex{ -1 };
        std::int32_t NextNodeIndex{ -1 };
        float CurrentLocalTime{};
        float CurrentNormalizedTime{};
        float BlendElapsed{};
        float BlendDuration{};
        bool IsInTransition{};
        bool PendingInterrupt{};

        std::int32_t SampleSourceClipIndex{ -1 };
        std::int32_t SampleDestinationClipIndex{ -1 };
        float SampleSourceLocalTime{};
        float SampleDestinationLocalTime{};
        float SampleBlendAlpha{};
        float SamplePlaySpeed{ 1.0f };
        bool SampleIsLoop{ true };
    EndComponent(AnimatorGraphPlayer)
}
