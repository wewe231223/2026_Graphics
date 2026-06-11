#pragma once

#include "Arche/Common.h"
#include "Utility/ComponentRestraint.h"
#include "DirectXTK12/SimpleMath.h"

namespace SimpleMath = DirectX::SimpleMath;

namespace Game {
    ComponentDecl(
        Transform,
        ComponentFields(
            ComponentField(::SimpleMath::Vector3, position, ::SimpleMath::Vector3::Zero)
            ComponentField(::SimpleMath::Vector3, rotationEuler, ::SimpleMath::Vector3::Zero)
            ComponentField(::SimpleMath::Quaternion, rotation, ::SimpleMath::Quaternion::Identity)
            ComponentField(::SimpleMath::Vector3, scale, ::SimpleMath::Vector3::One)
            ComponentField(::SimpleMath::Matrix, nodeToParent, ::SimpleMath::Matrix::Identity)
            ComponentField(::SimpleMath::Matrix, worldMatrix, ::SimpleMath::Matrix::Identity)
            ComponentField(::SimpleMath::Vector3, mCachedPosition, ::SimpleMath::Vector3::Zero)
            ComponentField(::SimpleMath::Quaternion, mCachedRotation, ::SimpleMath::Quaternion::Identity)
            ComponentField(::SimpleMath::Vector3, mCachedScale, ::SimpleMath::Vector3::One)
            ComponentField(::SimpleMath::Matrix, mCachedNodeToParent, ::SimpleMath::Matrix::Identity)
            ComponentField(Arche::EntityID, mCachedParentEntityId, Arche::NullEntityID)
            ComponentField(bool, mWorldMatrixCacheValid, false)
            ComponentField(bool, mWorldMatrixChanged, true)
        ),
        ComponentMethods(
            ComponentMethodNamed(void Translate(const SimpleMath::Vector3& Translation); void Translate(float X, float Y, float Z); void TranslateForLua(const SimpleMath::Vector3& Translation), TranslateForLua, Translate)
            ComponentMethodNamed(void Rotate(float YawDegrees, float PitchDegrees, float RollDegrees); void Rotate(const SimpleMath::Vector3& YawPitchRollDegrees); void RotateForLua(const SimpleMath::Vector3& YawPitchRollDegrees), RotateForLua, Rotate)
            ComponentMethod(void RotateRadians(float PitchRadians, float YawRadians, float RollRadians), RotateRadians)
            ComponentMethod(void ClampPitchRadians(float MinPitchRadians, float MaxPitchRadians), ClampPitchRadians)
            ComponentMethod(void UpdateRotationFromEulerRadians(), UpdateRotationFromEulerRadians)
            ComponentMethod(void UpdateEulerRadiansFromRotation(), UpdateEulerRadiansFromRotation)
            ComponentMethod(SimpleMath::Vector3 GetForwardDirection() const, GetForwardDirection)
            ComponentMethod(SimpleMath::Vector3 TransformDirectionToWorld(const SimpleMath::Vector3& LocalDirection) const, TransformDirectionToWorld)
            ComponentMethodNamed(void Scaling(float X, float Y, float Z); void Scaling(const SimpleMath::Vector3& Scale); void ScalingForLua(const SimpleMath::Vector3& Scale), ScalingForLua, Scaling)
            ComponentMethod(void Look(const SimpleMath::Vector3& Target), Look)
            ComponentMethod(bool IsBehind(const SimpleMath::Vector3& Target) const, IsBehind)
            ComponentMethod(bool IsFacing(const SimpleMath::Vector3& Target, float AngleDegrees) const, IsFacing)
            ComponentMethod(float GetDistanceSquaredTo(const SimpleMath::Vector3& Target) const, GetDistanceSquaredTo)
            ComponentMethod(void Reset(), Reset)
        )
    );
}
