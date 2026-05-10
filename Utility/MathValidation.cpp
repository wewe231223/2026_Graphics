#include "Utility/MathValidation.h"

#include <cmath>

#include "Utility/DirectXInclude.h"

namespace MathUtility {
    bool IsFiniteFloat(const float Value) {
        return std::isfinite(Value) != 0;
    }

    bool IsFiniteVector3(const DirectX::SimpleMath::Vector3& Value) {
        return IsFiniteFloat(Value.x) == true && IsFiniteFloat(Value.y) == true && IsFiniteFloat(Value.z) == true;
    }

    bool IsFiniteQuaternion(const DirectX::SimpleMath::Quaternion& Value) {
        return IsFiniteFloat(Value.x) == true && IsFiniteFloat(Value.y) == true && IsFiniteFloat(Value.z) == true && IsFiniteFloat(Value.w) == true;
    }
}
