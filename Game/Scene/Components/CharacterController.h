#pragma once

#include <DirectXTK12/SimpleMath.h>

#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        CharacterController,
        ComponentFields(
            ComponentField(bool, mIsActive, true)
            ComponentField(float, mHorizontalAcceleration, 900.0F)
            ComponentField(float, mJumpSpeed, 8.0F)
            ComponentField(float, mGroundSnapDistance, 0.25F)
            ComponentField(DirectX::SimpleMath::Vector3, mDesiredHorizontalVelocity, DirectX::SimpleMath::Vector3::Zero)
            ComponentField(DirectX::SimpleMath::Vector3, mVelocity, DirectX::SimpleMath::Vector3::Zero)
            ComponentField(bool, mIsGrounded, false)
            ComponentField(bool, mHasJumpRequest, false)
        ),
        ComponentMethods(
            ComponentMethod(void SetDesiredHorizontalVelocity(const DirectX::SimpleMath::Vector3& DesiredHorizontalVelocity), SetDesiredHorizontalVelocity)
            ComponentMethod(void RequestJump(), RequestJump)
            ComponentMethod(DirectX::SimpleMath::Vector3 GetVelocity() const, GetVelocity)
            ComponentMethod(bool GetIsGrounded() const, GetIsGrounded)
        )
    );
}
