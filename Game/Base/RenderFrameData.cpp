#include "RenderFrameData.h"
#include <algorithm>
#include <cmath>

namespace {
    constexpr float MinimumDebugGeometryLineThickness{ 0.0001f };
    constexpr float MinimumDebugGeometryDirectionLength{ 0.0001f };
    constexpr float MinimumDebugGeometryExtent{ 0.0001f };

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

    float ResolveProjectionDivisor(float Value, float MinimumProjectionDivisor) {
        if (std::abs(Value) > MinimumProjectionDivisor) {
            return Value;
        }

        return Value < 0.0f ? -MinimumProjectionDivisor : MinimumProjectionDivisor;
    }

    DirectX::BoundingOrientedBox BuildShadowCullingBox(const Game::RFD::CameraParameter& ShadowCameraParameter, float MinimumProjectionDivisor, float MinimumProjectionDepthSpan) {
        const SimpleMath::Matrix& ProjectionMatrix{ ShadowCameraParameter.proj };
        const float ProjectionScaleX{ ResolveProjectionDivisor(ProjectionMatrix._11, MinimumProjectionDivisor) };
        const float ProjectionScaleY{ ResolveProjectionDivisor(ProjectionMatrix._22, MinimumProjectionDivisor) };
        const float ProjectionCenterX{ -ProjectionMatrix._41 / ProjectionScaleX };
        const float ProjectionCenterY{ -ProjectionMatrix._42 / ProjectionScaleY };
        const float ProjectionHalfWidth{ std::abs(1.0f / ProjectionScaleX) };
        const float ProjectionHalfHeight{ std::abs(1.0f / ProjectionScaleY) };
        const float EffectiveNearPlane{ std::max(ShadowCameraParameter.nearPlane, 0.0f) };
        const float EffectiveFarPlane{ std::max(ShadowCameraParameter.farPlane, EffectiveNearPlane + MinimumProjectionDepthSpan) };
        const float ProjectionHalfDepth{ (EffectiveFarPlane - EffectiveNearPlane) * 0.5f };
        const float ProjectionCenterZ{ EffectiveNearPlane + ProjectionHalfDepth };

        DirectX::BoundingOrientedBox LightSpaceBox{};
        LightSpaceBox.Center = DirectX::XMFLOAT3{ ProjectionCenterX, ProjectionCenterY, ProjectionCenterZ };
        LightSpaceBox.Extents = DirectX::XMFLOAT3{ ProjectionHalfWidth, ProjectionHalfHeight, ProjectionHalfDepth };
        LightSpaceBox.Orientation = DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f };

        DirectX::BoundingOrientedBox WorldSpaceBox{};
        const SimpleMath::Matrix InverseViewMatrix{ ShadowCameraParameter.view.Invert() };
        LightSpaceBox.Transform(WorldSpaceBox, InverseViewMatrix);
        return WorldSpaceBox;
    }
}

namespace Game {
    namespace RFD {
        void DebugGeometryContext::SetLine(const SimpleMath::Vector3& StartPosition, const SimpleMath::Vector3& EndPosition, const SimpleMath::Vector4& ColorValue, float LineThicknessValue) {
            const float ResolvedLineThickness{ ResolveMinimumPositiveValue(LineThicknessValue, MinimumDebugGeometryLineThickness) };

            parameter0 = SimpleMath::Vector4{ StartPosition.x, StartPosition.y, StartPosition.z, 1.0f };
            parameter1 = SimpleMath::Vector4{ EndPosition.x, EndPosition.y, EndPosition.z, 1.0f };
            parameter2 = SimpleMath::Vector4{ 0.0f, 0.0f, 0.0f, 1.0f };
            color = ColorValue;
            type = DebugGeometryTypeLine;
            lineThickness = ResolvedLineThickness;
            padding0 = 0u;
            padding1 = 0u;
        }

        void DebugGeometryContext::SetDirection(const SimpleMath::Vector3& Origin, const SimpleMath::Vector3& Direction, float Length, const SimpleMath::Vector4& ColorValue, float LineThicknessValue) {
            const SimpleMath::Vector3 DirectionUnitVector{ ResolveDirectionUnitVector(Direction) };
            const float ResolvedDirectionLength{ ResolveMinimumPositiveValue(Length, MinimumDebugGeometryDirectionLength) };
            const SimpleMath::Vector3 EndPosition{ Origin + (DirectionUnitVector * ResolvedDirectionLength) };
            SetLine(Origin, EndPosition, ColorValue, LineThicknessValue);
        }

        void DebugGeometryContext::SetWireCube(const SimpleMath::Vector3& CenterValue, const SimpleMath::Vector3& ExtentsValue, const SimpleMath::Quaternion& OrientationValue, const SimpleMath::Vector4& ColorValue, float LineThicknessValue) {
            const float ResolvedLineThickness{ ResolveMinimumPositiveValue(LineThicknessValue, MinimumDebugGeometryLineThickness) };
            const SimpleMath::Vector3 ResolvedExtents{ ResolveMinimumPositiveValue(std::fabs(ExtentsValue.x), MinimumDebugGeometryExtent), ResolveMinimumPositiveValue(std::fabs(ExtentsValue.y), MinimumDebugGeometryExtent), ResolveMinimumPositiveValue(std::fabs(ExtentsValue.z), MinimumDebugGeometryExtent) };
            const SimpleMath::Quaternion ResolvedOrientation{ ResolveUnitQuaternion(OrientationValue) };

            parameter0 = SimpleMath::Vector4{ CenterValue.x, CenterValue.y, CenterValue.z, 1.0f };
            parameter1 = SimpleMath::Vector4{ ResolvedExtents.x, ResolvedExtents.y, ResolvedExtents.z, 0.0f };
            parameter2 = SimpleMath::Vector4{ ResolvedOrientation.x, ResolvedOrientation.y, ResolvedOrientation.z, ResolvedOrientation.w };
            color = ColorValue;
            type = DebugGeometryTypeWireCube;
            lineThickness = ResolvedLineThickness;
            padding0 = 0u;
            padding1 = 0u;
        }

        DebugGeometryContext DebugGeometryContext::CreateLine(const SimpleMath::Vector3& StartPosition, const SimpleMath::Vector3& EndPosition, const SimpleMath::Vector4& ColorValue, float LineThicknessValue) {
            DebugGeometryContext NewDebugGeometryContext{};
            NewDebugGeometryContext.SetLine(StartPosition, EndPosition, ColorValue, LineThicknessValue);
            return NewDebugGeometryContext;
        }

        DebugGeometryContext DebugGeometryContext::CreateDirection(const SimpleMath::Vector3& Origin, const SimpleMath::Vector3& Direction, float Length, const SimpleMath::Vector4& ColorValue, float LineThicknessValue) {
            DebugGeometryContext NewDebugGeometryContext{};
            NewDebugGeometryContext.SetDirection(Origin, Direction, Length, ColorValue, LineThicknessValue);
            return NewDebugGeometryContext;
        }

        DebugGeometryContext DebugGeometryContext::CreateWireCube(const SimpleMath::Vector3& CenterValue, const SimpleMath::Vector3& ExtentsValue, const SimpleMath::Quaternion& OrientationValue, const SimpleMath::Vector4& ColorValue, float LineThicknessValue) {
            DebugGeometryContext NewDebugGeometryContext{};
            NewDebugGeometryContext.SetWireCube(CenterValue, ExtentsValue, OrientationValue, ColorValue, LineThicknessValue);
            return NewDebugGeometryContext;
        }

        std::uint32_t ResolveShadowCascadeCount(const ShadowMappingParameter& ShadowMappingParameter) {
            return std::max(1u, std::min(ShadowMappingParameter.cascadeCount, ShadowCascadeMaxCount));
        }

        std::array<DirectX::BoundingOrientedBox, ShadowCascadeMaxCount> BuildShadowCullingBoxes(const ShadowMappingParameter& ShadowMappingParameter) {
            std::array<DirectX::BoundingOrientedBox, ShadowCascadeMaxCount> ShadowCullingBoxes{};
            const std::uint32_t ShadowCascadeCount{ ResolveShadowCascadeCount(ShadowMappingParameter) };

            for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1) {
                ShadowCullingBoxes[CascadeIndex] = BuildShadowCullingBox(ShadowMappingParameter.shadowCameras[CascadeIndex], ShadowMappingParameter.minimumProjectionDivisor, ShadowMappingParameter.minimumProjectionDepthSpan);
            }

            return ShadowCullingBoxes;
        }
    }
}
