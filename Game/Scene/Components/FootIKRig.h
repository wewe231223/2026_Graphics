#pragma once

#include <string_view>
#include "Game/Scene/Components/ComponentText.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        FootIKRig,
        ComponentFields(
            ComponentField(bool, mEnabled, false)
            ComponentField(ComponentTextArray, mLeftFootBoneName, {})
            ComponentField(ComponentTextArray, mRightFootBoneName, {})
            ComponentField(float, mFootSoleOffset, 0.12f)
            ComponentField(float, mBlendSpeed, 12.0f)
            ComponentField(float, mMaxLift, 0.35f)
        ),
        BOOST_PP_SEQ_NIL
    );

    void SetFootIKRigBoneName(ComponentTextArray& TargetText, ::std::string_view SourceText);
    const char* GetFootIKRigBoneNameText(const ComponentTextArray& SourceText);
}
