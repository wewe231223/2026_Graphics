#pragma once 

#include "Utility/ComponentRestraint.h"
#include "DirectXTK12/SimpleMath.h"

namespace SimpleMath = DirectX::SimpleMath;

namespace Game {

	Component(Transform)
		SimpleMath::Vector3 position{};
		SimpleMath::Vector3 rotationEuler{};
		SimpleMath::Quaternion rotation{};
		SimpleMath::Vector3 scale{ 1.0f, 1.0f, 1.0f };

		std::string a{}; 
	EndComponent(Transform)

}
