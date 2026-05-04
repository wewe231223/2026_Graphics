#pragma once

#include "Utility/ComponentRestraint.h"
#include "DirectXTK12/SimpleMath.h"

namespace Game {
    ComponentDecl(
        DirectionalLight,
        ComponentFields(
            ComponentField(bool, mIsActive, true)
            ComponentField(bool, mCastsShadow, true)
            ComponentField(bool, mUseTransformDirection, false)
            ComponentField(DirectX::SimpleMath::Vector3, mDirection, DirectX::SimpleMath::Vector3::Down)
            ComponentField(DirectX::SimpleMath::Vector3, mColor, DirectX::SimpleMath::Vector3::One)
            ComponentField(float, mIntensity, 1.2f)
            ComponentField(float, mAmbientIntensity, 0.25f)
        ),
        BOOST_PP_SEQ_NIL
    );
}
