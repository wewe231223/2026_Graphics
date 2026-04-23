#pragma once

#include <cstdint>
#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        AnimatorGraphPlayer,
        ComponentFields(
            ComponentField(std::int32_t, CurrentNodeIndex, -1)
            ComponentField(std::int32_t, NextNodeIndex, -1)
            ComponentField(float, CurrentLocalTime, 0.0f)
            ComponentField(float, CurrentNormalizedTime, 0.0f)
            ComponentField(float, BlendElapsed, 0.0f)
            ComponentField(float, BlendDuration, 0.0f)
            ComponentField(bool, IsInTransition, false)
            ComponentField(bool, PendingInterrupt, false)
            ComponentField(std::int32_t, SampleSourceClipIndex, -1)
            ComponentField(std::int32_t, SampleDestinationClipIndex, -1)
            ComponentField(float, SampleSourceLocalTime, 0.0f)
            ComponentField(float, SampleDestinationLocalTime, 0.0f)
            ComponentField(float, SampleBlendAlpha, 0.0f)
            ComponentField(float, SamplePlaySpeed, 1.0f)
            ComponentField(bool, SampleIsLoop, true)
        ),
        BOOST_PP_SEQ_NIL
    );
}
