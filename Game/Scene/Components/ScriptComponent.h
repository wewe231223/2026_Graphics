#pragma once

#include <cstdint>
#include <string_view>
#include "Arche/Common.h"
#include "Game/Scene/Components/ComponentText.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        BehaviorInstanceComponent,
        ComponentFields(
            ComponentField(Arche::EntityID, mOwnerEntity)
            ComponentField(::std::uint32_t, mBehaviorInstanceId)
            ComponentField(::std::uint32_t, mBehaviorAssetId)
            ComponentField(ComponentTextArray, mScriptFileName, {})
        ),
        BOOST_PP_SEQ_NIL
    );

    void SetScriptFileName(BehaviorInstanceComponent& TargetComponent, ::std::string_view FileName);
    const char* GetScriptFileName(const BehaviorInstanceComponent& TargetComponent);
}
