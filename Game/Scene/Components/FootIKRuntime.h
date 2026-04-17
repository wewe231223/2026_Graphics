#pragma once

#include "Arche/Common.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        FootIKRuntime,
        ComponentFields(
            ComponentField(Arche::EntityID, mLeftFootEntityId, Arche::NullEntityID)
            ComponentField(Arche::EntityID, mRightFootEntityId, Arche::NullEntityID)
            ComponentField(float, mLeftCurrentLift, 0.0f)
            ComponentField(float, mRightCurrentLift, 0.0f)
            ComponentField(float, mCurrentLift, 0.0f)
            ComponentField(bool, mResolved, false)
        ),
        BOOST_PP_SEQ_NIL
    );
}
