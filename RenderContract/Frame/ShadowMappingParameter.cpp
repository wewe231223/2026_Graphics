#include "RenderContract/Frame/ShadowMappingParameter.h"

#include <algorithm>
#include <cmath>

using namespace RenderContract;

namespace {
    float ResolveProjectionDivisor(float Value, float MinimumProjectionDivisor) {
        if (std::abs(Value) > MinimumProjectionDivisor) {
            return Value;
        }

        return Value < 0.0f ? -MinimumProjectionDivisor : MinimumProjectionDivisor;
    }

    DirectX::BoundingOrientedBox BuildShadowCullingBox(const CameraParameter& ShadowCameraParameter, float MinimumProjectionDivisor, float MinimumProjectionDepthSpan) {
        const DirectX::SimpleMath::Matrix& ProjectionMatrix{ ShadowCameraParameter.mProj };
        const float ProjectionScaleX{ ResolveProjectionDivisor(ProjectionMatrix._11, MinimumProjectionDivisor) };
        const float ProjectionScaleY{ ResolveProjectionDivisor(ProjectionMatrix._22, MinimumProjectionDivisor) };
        const float ProjectionCenterX{ -ProjectionMatrix._41 / ProjectionScaleX };
        const float ProjectionCenterY{ -ProjectionMatrix._42 / ProjectionScaleY };
        const float ProjectionHalfWidth{ std::abs(1.0f / ProjectionScaleX) };
        const float ProjectionHalfHeight{ std::abs(1.0f / ProjectionScaleY) };
        const float EffectiveNearPlane{ std::max(ShadowCameraParameter.mNearPlane, 0.0f) };
        const float EffectiveFarPlane{ std::max(ShadowCameraParameter.mFarPlane, EffectiveNearPlane + MinimumProjectionDepthSpan) };
        const float ProjectionHalfDepth{ (EffectiveFarPlane - EffectiveNearPlane) * 0.5f };
        const float ProjectionCenterZ{ EffectiveNearPlane + ProjectionHalfDepth };

        DirectX::BoundingOrientedBox LightSpaceBox{};
        LightSpaceBox.Center = DirectX::XMFLOAT3{ ProjectionCenterX, ProjectionCenterY, ProjectionCenterZ };
        LightSpaceBox.Extents = DirectX::XMFLOAT3{ ProjectionHalfWidth, ProjectionHalfHeight, ProjectionHalfDepth };
        LightSpaceBox.Orientation = DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f };

        DirectX::BoundingOrientedBox WorldSpaceBox{};
        const DirectX::SimpleMath::Matrix InverseViewMatrix{ ShadowCameraParameter.mView.Invert() };
        LightSpaceBox.Transform(WorldSpaceBox, InverseViewMatrix);
        return WorldSpaceBox;
    }
}

std::uint32_t RenderContract::ResolveShadowCascadeCount(const ShadowMappingParameter& ShadowMappingParameterValue) {
    return std::min(ShadowMappingParameterValue.mCascadeCount, ShadowCascadeMaxCount);
}

std::array<DirectX::BoundingOrientedBox, ShadowCascadeMaxCount> RenderContract::BuildShadowCullingBoxes(const ShadowMappingParameter& ShadowMappingParameterValue) {
    std::array<DirectX::BoundingOrientedBox, ShadowCascadeMaxCount> ShadowCullingBoxes{};
    const std::uint32_t ShadowCascadeCount{ ResolveShadowCascadeCount(ShadowMappingParameterValue) };

    for (std::uint32_t CascadeIndex{}; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1u) {
        ShadowCullingBoxes[CascadeIndex] = BuildShadowCullingBox(ShadowMappingParameterValue.mShadowCameras[CascadeIndex], ShadowMappingParameterValue.mMinimumProjectionDivisor, ShadowMappingParameterValue.mMinimumProjectionDepthSpan);
    }

    return ShadowCullingBoxes;
}
