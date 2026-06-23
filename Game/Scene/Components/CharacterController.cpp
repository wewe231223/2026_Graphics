#include "Game/Scene/Components/CharacterController.h"

namespace Game {
    void CharacterController::SetDesiredHorizontalVelocity(const DirectX::SimpleMath::Vector3& DesiredHorizontalVelocity) {
        mDesiredHorizontalVelocity = DirectX::SimpleMath::Vector3{ DesiredHorizontalVelocity.x, 0.0F, DesiredHorizontalVelocity.z };
    }

    void CharacterController::RequestJump() {
        mHasJumpRequest = true;
    }

    DirectX::SimpleMath::Vector3 CharacterController::GetVelocity() const {
        return mVelocity;
    }

    bool CharacterController::GetIsGrounded() const {
        return mIsGrounded;
    }
}
