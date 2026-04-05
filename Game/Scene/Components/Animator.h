#pragma once

#include <cstdint>
#include "Asset/AnimationClipResult.h"
#include "Game/Asset/AnimationGraphAsset.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        Animator,
        ComponentFields(
            ComponentField(asset::Animation*, animation, nullptr)
            ComponentField(::std::int32_t, clipIndex, -1)
            ComponentField(double, counter, 0.0)
            ComponentField(AnimationGraphAsset*, GraphAsset, nullptr)
            ComponentField(bool, IsGraphEnabled, false)
            ComponentField(::std::int32_t, FallbackClipIndex, -1)
        ),
        BOOST_PP_SEQ_NIL
    );
}
