#pragma once

#include <cstdint>

#include "DirectXTK12/SimpleMath.h"

namespace RenderContract {
    constexpr std::uint32_t DebugGeometryTypeLine{};
    constexpr std::uint32_t DebugGeometryTypeWireCube{ 1u };

    struct alignas(16) DebugGeometryContext final {
    public:
        DirectX::SimpleMath::Vector4 mParameter0{};
        DirectX::SimpleMath::Vector4 mParameter1{};
        DirectX::SimpleMath::Vector4 mParameter2{};
        DirectX::SimpleMath::Vector4 mColor{};
        std::uint32_t mType{ DebugGeometryTypeLine };
        float mLineThickness{ 0.0025f };
        std::uint32_t mPadding0{};
        std::uint32_t mPadding1{};

        void SetLine(const DirectX::SimpleMath::Vector3& StartPosition, const DirectX::SimpleMath::Vector3& EndPosition, const DirectX::SimpleMath::Vector4& ColorValue, float LineThicknessValue);
        void SetDirection(const DirectX::SimpleMath::Vector3& Origin, const DirectX::SimpleMath::Vector3& Direction, float Length, const DirectX::SimpleMath::Vector4& ColorValue, float LineThicknessValue);
        void SetWireCube(const DirectX::SimpleMath::Vector3& CenterValue, const DirectX::SimpleMath::Vector3& ExtentsValue, const DirectX::SimpleMath::Quaternion& OrientationValue, const DirectX::SimpleMath::Vector4& ColorValue, float LineThicknessValue);

        static DebugGeometryContext CreateLine(const DirectX::SimpleMath::Vector3& StartPosition, const DirectX::SimpleMath::Vector3& EndPosition, const DirectX::SimpleMath::Vector4& ColorValue, float LineThicknessValue);
        static DebugGeometryContext CreateDirection(const DirectX::SimpleMath::Vector3& Origin, const DirectX::SimpleMath::Vector3& Direction, float Length, const DirectX::SimpleMath::Vector4& ColorValue, float LineThicknessValue);
        static DebugGeometryContext CreateWireCube(const DirectX::SimpleMath::Vector3& CenterValue, const DirectX::SimpleMath::Vector3& ExtentsValue, const DirectX::SimpleMath::Quaternion& OrientationValue, const DirectX::SimpleMath::Vector4& ColorValue, float LineThicknessValue);
    };
}
