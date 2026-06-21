#pragma once

#include "Arche/Common.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        FootIKRuntime,
        ComponentFields(
            ComponentField(Arche::EntityID, mLeftFootEntityId, Arche::NullEntityID)
            ComponentField(Arche::EntityID, mRightFootEntityId, Arche::NullEntityID)
            ComponentField(Arche::EntityID, mLeftToeEntityId, Arche::NullEntityID)
            ComponentField(Arche::EntityID, mRightToeEntityId, Arche::NullEntityID)
            ComponentField(Arche::EntityID, mLeftShinEntityId, Arche::NullEntityID)
            ComponentField(Arche::EntityID, mRightShinEntityId, Arche::NullEntityID)
            ComponentField(Arche::EntityID, mLeftThighEntityId, Arche::NullEntityID)
            ComponentField(Arche::EntityID, mRightThighEntityId, Arche::NullEntityID)
            ComponentField(Arche::EntityID, mPelvisEntityId, Arche::NullEntityID)
            ComponentField(float, mLeftCurrentOffset, 0.0f)
            ComponentField(float, mRightCurrentOffset, 0.0f)
            ComponentField(bool, mResolved, false)
        ),
        BOOST_PP_SEQ_NIL
    );
}
