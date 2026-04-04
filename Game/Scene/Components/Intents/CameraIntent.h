#pragma once
#include <cstdint>
#include "Utility/ComponentRestraint.h"
#include "Utility/DirectXInclude.h"
#include "Arche/Common.h"

/**
 * @file CameraIntent.h
 * @brief 유저나 AI, 연출 시스템이 '이번 프레임'에 카메라에게 요구하는 휘발성 명령입니다.
 */

namespace Game {
    ComponentDecl(
        CameraIntent,
        ComponentFields(
            ComponentField(SimpleMath::Vector3, moveDirection, SimpleMath::Vector3::Zero)
            ComponentField(SimpleMath::Vector2, lookDelta, SimpleMath::Vector2::Zero)
            ComponentField(float, zoomDelta, 0.0f)
            ComponentField(Arche::EntityID, targetToLockOn, Arche::NullEntityID)
            ComponentField(bool, requestUnlock, false)
            ComponentField(bool, requestSkip, false)
            ComponentField(float, shakeImpulse, 0.0f)
        ),
        ComponentMethods(
            ComponentMethod(void Reset(), Reset)
        )
    );
}
