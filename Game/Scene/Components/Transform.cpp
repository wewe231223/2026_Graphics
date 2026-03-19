#include "Game/Scene/Components/Transform.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include "Game/Scene/Components/ComponentInspection.h"

namespace {
    std::string FormatVector3(const SimpleMath::Vector3& Value) {
        return std::format("{:.3f}, {:.3f}, {:.3f}", Value.x, Value.y, Value.z);
    }

    std::string FormatMatrixRow(float M11, float M12, float M13, float M14) {
        return std::format("[{:>8.3f} {:>8.3f} {:>8.3f} {:>8.3f}]", M11, M12, M13, M14);
    }

    std::string FormatMatrix(const SimpleMath::Matrix& Value) {
        return std::format(
            "{}\n{}\n{}\n{}",
            FormatMatrixRow(Value._11, Value._12, Value._13, Value._14),
            FormatMatrixRow(Value._21, Value._22, Value._23, Value._24),
            FormatMatrixRow(Value._31, Value._32, Value._33, Value._34),
            FormatMatrixRow(Value._41, Value._42, Value._43, Value._44)
        );
    }
}

namespace Game {
    const char* Transform::GetComponentInspectionName() {
        return "Transform";
    }

    void Transform::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "Position", FormatVector3(position) });
        OutFields.push_back(ComponentInspectionField{ "Rotation", FormatVector3(rotationEuler) });
        OutFields.push_back(ComponentInspectionField{ "Scale", FormatVector3(scale) });
        OutFields.push_back(ComponentInspectionField{ "Geometry To Node", FormatMatrix(geometryToNode) });
        OutFields.push_back(ComponentInspectionField{ "Node To Parent", FormatMatrix(nodeToParent) });
    }

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

    void Transform::RotateRadians(float PitchRadians, float YawRadians, float RollRadians) {
        rotationEuler += SimpleMath::Vector3{ PitchRadians, YawRadians, RollRadians };
        UpdateRotationFromEulerRadians();
    }

    void Transform::ClampPitchRadians(float MinPitchRadians, float MaxPitchRadians) {
        rotationEuler.x = std::clamp(rotationEuler.x, MinPitchRadians, MaxPitchRadians);
        UpdateRotationFromEulerRadians();
    }

    void Transform::UpdateRotationFromEulerRadians() {
        rotation = SimpleMath::Quaternion::CreateFromYawPitchRoll(rotationEuler.y, rotationEuler.x, rotationEuler.z);
        rotation.Normalize();
    }

    SimpleMath::Vector3 Transform::GetForwardDirection() const {
        return SimpleMath::Vector3::Transform(SimpleMath::Vector3::Forward, rotation);
    }

    SimpleMath::Vector3 Transform::TransformDirectionToWorld(const SimpleMath::Vector3& LocalDirection) const {
        return SimpleMath::Vector3::Transform(LocalDirection, rotation);
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
        const SimpleMath::Vector3 ForwardDirection{ GetForwardDirection() };
        const float FacingDot{ ForwardDirection.Dot(ToTarget) };

        return FacingDot < 0.0f;
    }

    bool Transform::IsFacing(const SimpleMath::Vector3& Target, float AngleDegrees) const {
        const SimpleMath::Vector3 ToTarget{ Target - position };
        const float DistanceSquared{ ToTarget.LengthSquared() };

        if (DistanceSquared <= 0.000001f) {
            return true;
        }

        SimpleMath::Vector3 ForwardDirection{ GetForwardDirection() };
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
        geometryToNode = SimpleMath::Matrix::Identity;
        nodeToParent = SimpleMath::Matrix::Identity;
    }

}
