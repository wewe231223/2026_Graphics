#include "Game/Scene/Components/Transform.h"

#include <cmath>

namespace Game {

    void Transform::Translate(const SimpleMath::Vector3& Translation) {
        position += Translation;
    }

    void Transform::Translate(float X, float Y, float Z) {
        Translate(SimpleMath::Vector3{ X, Y, Z });
    }

    void Transform::Rotate(float YawDegrees, float PitchDegrees, float RollDegrees) {
        const float YawRadians{ DirectX::XMConvertToRadians(YawDegrees) };
        const float PitchRadians{ DirectX::XMConvertToRadians(PitchDegrees) };
        const float RollRadians{ DirectX::XMConvertToRadians(RollDegrees) };
        const SimpleMath::Quaternion DeltaRotation{ SimpleMath::Quaternion::CreateFromYawPitchRoll(YawRadians, PitchRadians, RollRadians) };

        rotation *= DeltaRotation;
        rotation.Normalize();
        rotationEuler += SimpleMath::Vector3{ PitchRadians, YawRadians, RollRadians };
    }

    void Transform::Rotate(const SimpleMath::Vector3& YawPitchRollDegrees) {
        Rotate(YawPitchRollDegrees.x, YawPitchRollDegrees.y, YawPitchRollDegrees.z);
    }

    void Transform::Scaling(float X, float Y, float Z) {
        scale = SimpleMath::Vector3{ X, Y, Z };
    }

    void Transform::Scaling(const SimpleMath::Vector3& Scale) {
        scale = Scale;
    }

    void Transform::Look(const SimpleMath::Vector3& Target) {
        const SimpleMath::Vector3 ToTarget{ Target - position };
        const float LengthSquared{ ToTarget.LengthSquared() };

        if (LengthSquared <= 0.000001f) {
            return;
        }

        SimpleMath::Vector3 ForwardDirection{ ToTarget };
        ForwardDirection.Normalize();

        SimpleMath::Vector3 UpDirection{ SimpleMath::Vector3::Up };
        const float UpDot{ std::fabs(ForwardDirection.Dot(UpDirection)) };

        if (UpDot >= 0.999f) {
            UpDirection = SimpleMath::Vector3::Right;
        }

        const SimpleMath::Matrix WorldMatrix{ SimpleMath::Matrix::CreateWorld(SimpleMath::Vector3::Zero, ForwardDirection, UpDirection) };
        rotation = SimpleMath::Quaternion::CreateFromRotationMatrix(WorldMatrix);
        rotation.Normalize();
    }

    bool Transform::IsBehind(const SimpleMath::Vector3& Target) const {
        const SimpleMath::Vector3 ToTarget{ Target - position };
        const SimpleMath::Vector3 ForwardDirection{ SimpleMath::Vector3::Transform(SimpleMath::Vector3::Forward, rotation) };
        const float FacingDot{ ForwardDirection.Dot(ToTarget) };

        return FacingDot < 0.0f;
    }

    bool Transform::IsFacing(const SimpleMath::Vector3& Target, float AngleDegrees) const {
        const SimpleMath::Vector3 ToTarget{ Target - position };
        const float DistanceSquared{ ToTarget.LengthSquared() };

        if (DistanceSquared <= 0.000001f) {
            return true;
        }

        SimpleMath::Vector3 ForwardDirection{ SimpleMath::Vector3::Transform(SimpleMath::Vector3::Forward, rotation) };
        ForwardDirection.Normalize();

        SimpleMath::Vector3 ToTargetNormalized{ ToTarget };
        ToTargetNormalized.Normalize();

        const float FacingDot{ ForwardDirection.Dot(ToTargetNormalized) };
        const float DotThreshold{ std::cos(DirectX::XMConvertToRadians(AngleDegrees)) };

        return FacingDot >= DotThreshold;
    }

    float Transform::GetDistanceSquaredTo(const SimpleMath::Vector3& Target) const {
        return SimpleMath::Vector3::DistanceSquared(position, Target);
    }

    void Transform::Reset() {
        position = SimpleMath::Vector3::Zero;
        rotationEuler = SimpleMath::Vector3::Zero;
        rotation = SimpleMath::Quaternion::Identity;
        scale = SimpleMath::Vector3{ 1.0f, 1.0f, 1.0f };
    }

}
