#include "RenderContract/Geometry/DebugGeometryContext.h"

#include <algorithm>
#include <cmath>

using namespace RenderContract;

namespace {
    constexpr float MinimumLineThickness{ 0.0001f };
    constexpr float MinimumDirectionLength{ 0.0001f };
    constexpr float MinimumExtent{ 0.0001f };

    float ResolveMinimumPositiveValue(float Value, float MinimumValue) {
        return std::max(Value, MinimumValue);
    }

    DirectX::SimpleMath::Vector3 ResolveDirectionUnitVector(const DirectX::SimpleMath::Vector3& Direction) {
        const float DirectionLengthSquared{ (Direction.x * Direction.x) + (Direction.y * Direction.y) + (Direction.z * Direction.z) };
        if (DirectionLengthSquared <= 0.0f) {
            return DirectX::SimpleMath::Vector3{ 0.0f, 0.0f, 1.0f };
        }

        const float DirectionLengthInverse{ 1.0f / std::sqrt(DirectionLengthSquared) };
        return DirectX::SimpleMath::Vector3{ Direction.x * DirectionLengthInverse, Direction.y * DirectionLengthInverse, Direction.z * DirectionLengthInverse };
    }

    DirectX::SimpleMath::Quaternion ResolveUnitQuaternion(const DirectX::SimpleMath::Quaternion& QuaternionValue) {
        const float QuaternionLengthSquared{ (QuaternionValue.x * QuaternionValue.x) + (QuaternionValue.y * QuaternionValue.y) + (QuaternionValue.z * QuaternionValue.z) + (QuaternionValue.w * QuaternionValue.w) };
        if (QuaternionLengthSquared <= 0.0f) {
            return DirectX::SimpleMath::Quaternion{ 0.0f, 0.0f, 0.0f, 1.0f };
        }

        const float QuaternionLengthInverse{ 1.0f / std::sqrt(QuaternionLengthSquared) };
        return DirectX::SimpleMath::Quaternion{ QuaternionValue.x * QuaternionLengthInverse, QuaternionValue.y * QuaternionLengthInverse, QuaternionValue.z * QuaternionLengthInverse, QuaternionValue.w * QuaternionLengthInverse };
    }
}

void DebugGeometryContext::SetLine(const DirectX::SimpleMath::Vector3& StartPosition, const DirectX::SimpleMath::Vector3& EndPosition, const DirectX::SimpleMath::Vector4& ColorValue, float LineThicknessValue) {
    mParameter0 = DirectX::SimpleMath::Vector4{ StartPosition.x, StartPosition.y, StartPosition.z, 1.0f };
    mParameter1 = DirectX::SimpleMath::Vector4{ EndPosition.x, EndPosition.y, EndPosition.z, 1.0f };
    mParameter2 = DirectX::SimpleMath::Vector4{ 0.0f, 0.0f, 0.0f, 1.0f };
    mColor = ColorValue;
    mType = DebugGeometryTypeLine;
    mLineThickness = ResolveMinimumPositiveValue(LineThicknessValue, MinimumLineThickness);
    mPadding0 = 0u;
    mPadding1 = 0u;
}

void DebugGeometryContext::SetDirection(const DirectX::SimpleMath::Vector3& Origin, const DirectX::SimpleMath::Vector3& Direction, float Length, const DirectX::SimpleMath::Vector4& ColorValue, float LineThicknessValue) {
    const DirectX::SimpleMath::Vector3 DirectionUnitVector{ ResolveDirectionUnitVector(Direction) };
    const float ResolvedDirectionLength{ ResolveMinimumPositiveValue(Length, MinimumDirectionLength) };
    SetLine(Origin, Origin + (DirectionUnitVector * ResolvedDirectionLength), ColorValue, LineThicknessValue);
}

void DebugGeometryContext::SetWireCube(const DirectX::SimpleMath::Vector3& CenterValue, const DirectX::SimpleMath::Vector3& ExtentsValue, const DirectX::SimpleMath::Quaternion& OrientationValue, const DirectX::SimpleMath::Vector4& ColorValue, float LineThicknessValue) {
    const DirectX::SimpleMath::Vector3 ResolvedExtents{ ResolveMinimumPositiveValue(std::fabs(ExtentsValue.x), MinimumExtent), ResolveMinimumPositiveValue(std::fabs(ExtentsValue.y), MinimumExtent), ResolveMinimumPositiveValue(std::fabs(ExtentsValue.z), MinimumExtent) };
    const DirectX::SimpleMath::Quaternion ResolvedOrientation{ ResolveUnitQuaternion(OrientationValue) };

    mParameter0 = DirectX::SimpleMath::Vector4{ CenterValue.x, CenterValue.y, CenterValue.z, 1.0f };
    mParameter1 = DirectX::SimpleMath::Vector4{ ResolvedExtents.x, ResolvedExtents.y, ResolvedExtents.z, 0.0f };
    mParameter2 = DirectX::SimpleMath::Vector4{ ResolvedOrientation.x, ResolvedOrientation.y, ResolvedOrientation.z, ResolvedOrientation.w };
    mColor = ColorValue;
    mType = DebugGeometryTypeWireCube;
    mLineThickness = ResolveMinimumPositiveValue(LineThicknessValue, MinimumLineThickness);
    mPadding0 = 0u;
    mPadding1 = 0u;
}

DebugGeometryContext DebugGeometryContext::CreateLine(const DirectX::SimpleMath::Vector3& StartPosition, const DirectX::SimpleMath::Vector3& EndPosition, const DirectX::SimpleMath::Vector4& ColorValue, float LineThicknessValue) {
    DebugGeometryContext NewContext{};
    NewContext.SetLine(StartPosition, EndPosition, ColorValue, LineThicknessValue);
    return NewContext;
}

DebugGeometryContext DebugGeometryContext::CreateDirection(const DirectX::SimpleMath::Vector3& Origin, const DirectX::SimpleMath::Vector3& Direction, float Length, const DirectX::SimpleMath::Vector4& ColorValue, float LineThicknessValue) {
    DebugGeometryContext NewContext{};
    NewContext.SetDirection(Origin, Direction, Length, ColorValue, LineThicknessValue);
    return NewContext;
}

DebugGeometryContext DebugGeometryContext::CreateWireCube(const DirectX::SimpleMath::Vector3& CenterValue, const DirectX::SimpleMath::Vector3& ExtentsValue, const DirectX::SimpleMath::Quaternion& OrientationValue, const DirectX::SimpleMath::Vector4& ColorValue, float LineThicknessValue) {
    DebugGeometryContext NewContext{};
    NewContext.SetWireCube(CenterValue, ExtentsValue, OrientationValue, ColorValue, LineThicknessValue);
    return NewContext;
}
