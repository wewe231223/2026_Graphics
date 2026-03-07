#pragma once 

#include "Utility/ComponentRestraint.h"
#include "DirectXTK12/SimpleMath.h"

namespace SimpleMath = DirectX::SimpleMath;

namespace Game {

	Component(Transform)
		void Translate(const SimpleMath::Vector3& Translation);
		void Translate(float X, float Y, float Z);

		void Rotate(float YawDegrees, float PitchDegrees, float RollDegrees);
		void Rotate(const SimpleMath::Vector3& YawPitchRollDegrees);
		void RotateRadians(float PitchRadians, float YawRadians, float RollRadians);
		void ClampPitchRadians(float MinPitchRadians, float MaxPitchRadians);
		void UpdateRotationFromEulerRadians();
		SimpleMath::Vector3 GetForwardDirection() const;
		SimpleMath::Vector3 TransformDirectionToWorld(const SimpleMath::Vector3& LocalDirection) const;

		void Scaling(float X, float Y, float Z);
		void Scaling(const SimpleMath::Vector3& Scale);

		void Look(const SimpleMath::Vector3& Target);

		bool IsBehind(const SimpleMath::Vector3& Target) const;
		bool IsFacing(const SimpleMath::Vector3& Target, float AngleDegrees) const;

		float GetDistanceSquaredTo(const SimpleMath::Vector3& Target) const;

		void Reset();

		SimpleMath::Vector3 position{};
		SimpleMath::Vector3 rotationEuler{};
		SimpleMath::Quaternion rotation{};
		SimpleMath::Vector3 scale{ 1.0f, 1.0f, 1.0f };
		SimpleMath::Matrix geometryToNode{ SimpleMath::Matrix::Identity };
		SimpleMath::Matrix nodeToParent{ SimpleMath::Matrix::Identity };
	EndComponent(Transform)

}
