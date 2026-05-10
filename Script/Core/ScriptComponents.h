#pragma once
#include <cstdint>
#include "Arche/Common.h"
#include "Utility/ComponentRestraint.h"

ComponentDecl(
    BehaviorInstanceComponent,
    ComponentFields(
        ComponentField(Arche::EntityID, mOwnerEntity)
        ComponentField(std::uint32_t, mBehaviorInstanceId)
        ComponentField(std::uint32_t, mBehaviorAssetId)
    ),
    BOOST_PP_SEQ_NIL
);
