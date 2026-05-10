#pragma once

namespace DirectX::SimpleMath {
    struct Vector3;
    struct Quaternion;
}

namespace MathUtility {
    bool IsFiniteFloat(float Value);
    bool IsFiniteVector3(const DirectX::SimpleMath::Vector3& Value);
    bool IsFiniteQuaternion(const DirectX::SimpleMath::Quaternion& Value);
}
