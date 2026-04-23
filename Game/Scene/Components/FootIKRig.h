#pragma once

#include <string_view>
#include "Game/Scene/Components/ComponentText.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        FootIKRig,
        ComponentFields(
            ComponentField(bool, mEnabled, false)
            ComponentField(ComponentTextArray, mLeftFootBoneName)
            ComponentField(ComponentTextArray, mRightFootBoneName)
            ComponentField(ComponentTextArray, mLeftToeBoneName)
            ComponentField(ComponentTextArray, mRightToeBoneName)
            ComponentField(ComponentTextArray, mLeftShinBoneName)
            ComponentField(ComponentTextArray, mRightShinBoneName)
            ComponentField(ComponentTextArray, mLeftThighBoneName)
            ComponentField(ComponentTextArray, mRightThighBoneName)
            ComponentField(ComponentTextArray, mPelvisBoneName)
            ComponentField(float, mBlendSpeed, 12.0f)
            ComponentField(float, mMaxLift, 0.35f)
            ComponentField(float, mMaxDrop, 0.20f)
        ),
        BOOST_PP_SEQ_NIL
    );

    void SetFootIKRigBoneName(ComponentTextArray& TargetText, std::string_view SourceText);
    std::string_view GetFootIKRigBoneNameText(const ComponentTextArray& SourceText);
}
