#include "ShadowMappingParameterSystem.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/Transform.h"


#undef min
#undef max 

namespace {
    struct ShadowCascadeRange {
        float nearPlane{ 0.0f };
        float farPlane{ 0.0f };
    };

    constexpr DirectX::SimpleMath::Vector3 ShadowLightDirection{ 0.4f, -1.0f, 0.35f };
    constexpr float ShadowMinimumNearPlane{ 0.1f };
    constexpr float ShadowMinimumExtent{ 1.0f };
    constexpr float ShadowMinimumViewDistance{ 2.0f };
    constexpr float ShadowMinimumFarOffset{ 0.25f };
    constexpr float ShadowNearPlaneOffset{ 0.25f };
    constexpr float ShadowFarPlaneOffset{ 0.35f };
    constexpr float ShadowProjectionSizeOffset{ 0.35f };
    constexpr float ShadowCameraBackOffset{ 4.0f };
    constexpr int ShadowCascadeCount{ 4 };
    constexpr int ShadowClosestCascadeIndex{ 0 };
    constexpr float ShadowClosestCascadeProjectionCoverageScale{ 1.0f };
    constexpr float ShadowCascadeSplitLambda{ 0.96f };
    constexpr float ParallelDirectionThreshold{ 0.98f };
    constexpr bool ShadowViewProjectionStabilizationEnabled{ false };

    float DotProduct(const DirectX::SimpleMath::Vector3& Left, const DirectX::SimpleMath::Vector3& Right) {
        return (Left.x * Right.x) + (Left.y * Right.y) + (Left.z * Right.z);
    }

    float SnapToGrid(float Value, float GridSize) {
        const float SafeGridSize{ std::max(GridSize, 0.000001f) };
        return std::roundf(Value / SafeGridSize) * SafeGridSize;
    }

    DirectX::SimpleMath::Vector3 NormalizeDirection(const DirectX::SimpleMath::Vector3& Direction) {
        DirectX::SimpleMath::Vector3 NormalizedDirection{ Direction };
        const float DirectionLengthSquared{ DotProduct(NormalizedDirection, NormalizedDirection) };
        if (DirectionLengthSquared <= 0.0f) {
            return DirectX::SimpleMath::Vector3::Forward;
        }

        NormalizedDirection.Normalize();
        return NormalizedDirection;
    }

    float ComputeCascadeSplitDistance(float CameraNearPlane, float CameraFarPlane, int CascadeIndex, int CascadeCount, float CascadeSplitLambda) {
        const float EffectiveNearPlane{ std::max(CameraNearPlane, ShadowMinimumNearPlane) };
        const float EffectiveFarPlane{ std::max(CameraFarPlane, EffectiveNearPlane + ShadowMinimumViewDistance) };
        const int EffectiveCascadeCount{ std::max(CascadeCount, 1) };
        const float SplitRatio{ static_cast<float>(CascadeIndex) / static_cast<float>(EffectiveCascadeCount) };
        const float UniformSplitDistance{ EffectiveNearPlane + ((EffectiveFarPlane - EffectiveNearPlane) * SplitRatio) };
        const float LogarithmicSplitDistance{ EffectiveNearPlane * std::pow(EffectiveFarPlane / EffectiveNearPlane, SplitRatio) };
        const float EffectiveCascadeSplitLambda{ std::clamp(CascadeSplitLambda, 0.0f, 1.0f) };
        return std::lerp(UniformSplitDistance, LogarithmicSplitDistance, EffectiveCascadeSplitLambda);
    }

    ShadowCascadeRange ComputeCascadeRange(const Game::Camera& CameraComponent, int CascadeIndex, int CascadeCount, float CascadeSplitLambda) {
        const float EffectiveNearPlane{ std::max(CameraComponent.nearPlane, ShadowMinimumNearPlane) };
        const float EffectiveFarPlane{ std::max(CameraComponent.farPlane, EffectiveNearPlane + ShadowMinimumViewDistance) };
        const int EffectiveCascadeIndex{ std::clamp(CascadeIndex, 0, std::max(CascadeCount - 1, 0)) };
        const float CascadeNearPlane{ ComputeCascadeSplitDistance(EffectiveNearPlane, EffectiveFarPlane, EffectiveCascadeIndex, CascadeCount, CascadeSplitLambda) };
        const float CascadeFarPlane{ ComputeCascadeSplitDistance(EffectiveNearPlane, EffectiveFarPlane, EffectiveCascadeIndex + 1, CascadeCount, CascadeSplitLambda) };
        ShadowCascadeRange CascadeRange{};
        CascadeRange.nearPlane = CascadeNearPlane;
        CascadeRange.farPlane = std::max(CascadeFarPlane, CascadeNearPlane + ShadowMinimumViewDistance);
        return CascadeRange;
    }

    std::array<DirectX::SimpleMath::Vector3, 8> BuildCameraFrustumCorners(const Game::Camera& CameraComponent, const Game::Transform& TransformComponent, const ShadowCascadeRange& CascadeRange) {
        (void)TransformComponent;

        const float EffectiveAspectRatio{ std::max(CameraComponent.aspectRatio, 0.01f) };
        const float EffectiveNearPlane{ std::max(CascadeRange.nearPlane, ShadowMinimumNearPlane) };
        const float EffectiveFarPlane{ std::max(CascadeRange.farPlane, EffectiveNearPlane + ShadowMinimumViewDistance) };

        DirectX::SimpleMath::Matrix EffectiveProjection{};
        if (CameraComponent.isOrthographic) {
            const float OrthographicHeight{ std::max(CameraComponent.orthoSize * 2.0f, ShadowMinimumExtent) };
            const float OrthographicWidth{ std::max(OrthographicHeight * EffectiveAspectRatio, ShadowMinimumExtent) };
            EffectiveProjection = DirectX::SimpleMath::Matrix::CreateOrthographic(OrthographicWidth, OrthographicHeight, EffectiveNearPlane, EffectiveFarPlane);
        }
        else {
            float FovRadians{ DirectX::XMConvertToRadians(CameraComponent.fov) };
            const float MinimumFovRadians{ DirectX::XMConvertToRadians(1.0f) };
            const float MaximumFovRadians{ DirectX::XMConvertToRadians(179.0f) };
            FovRadians = std::clamp(FovRadians, MinimumFovRadians, MaximumFovRadians);
            EffectiveProjection = DirectX::SimpleMath::Matrix::CreatePerspectiveFieldOfView(FovRadians, EffectiveAspectRatio, EffectiveNearPlane, EffectiveFarPlane);
        }

        const DirectX::SimpleMath::Matrix ViewProjectionInverse{ (CameraComponent.viewMatrix * EffectiveProjection).Invert() };
        constexpr std::array<DirectX::SimpleMath::Vector4, 8> NdcCorners{
            DirectX::SimpleMath::Vector4{ -1.0f, 1.0f, 0.0f, 1.0f },
            DirectX::SimpleMath::Vector4{ 1.0f, 1.0f, 0.0f, 1.0f },
            DirectX::SimpleMath::Vector4{ 1.0f, -1.0f, 0.0f, 1.0f },
            DirectX::SimpleMath::Vector4{ -1.0f, -1.0f, 0.0f, 1.0f },
            DirectX::SimpleMath::Vector4{ -1.0f, 1.0f, 1.0f, 1.0f },
            DirectX::SimpleMath::Vector4{ 1.0f, 1.0f, 1.0f, 1.0f },
            DirectX::SimpleMath::Vector4{ 1.0f, -1.0f, 1.0f, 1.0f },
            DirectX::SimpleMath::Vector4{ -1.0f, -1.0f, 1.0f, 1.0f }
        };

        std::array<DirectX::SimpleMath::Vector3, 8> FrustumCorners{};
        for (std::size_t Index{ 0 }; Index < NdcCorners.size(); Index += 1) {
            const DirectX::SimpleMath::Vector4 WorldCorner{ DirectX::SimpleMath::Vector4::Transform(NdcCorners[Index], ViewProjectionInverse) };
            const float WorldW{ std::abs(WorldCorner.w) <= 0.000001f ? 1.0f : WorldCorner.w };
            FrustumCorners[Index] = DirectX::SimpleMath::Vector3{ WorldCorner.x / WorldW, WorldCorner.y / WorldW, WorldCorner.z / WorldW };
        }

        return FrustumCorners;
    }

    void StabilizeShadowViewProjection(DirectX::SimpleMath::Matrix& InOutShadowViewProjection, float ShadowMapSize) {
        const float EffectiveShadowMapSize{ std::max(ShadowMapSize, 1.0f) };
        const float InverseShadowMapSize{ 1.0f / EffectiveShadowMapSize };
        InOutShadowViewProjection = DirectX::SimpleMath::Matrix::CreateScale(InverseShadowMapSize, InverseShadowMapSize, 1.0f) * InOutShadowViewProjection;

        for (int RowIndex{ 0 }; RowIndex < 3; RowIndex += 1) {
            InOutShadowViewProjection.m[3][RowIndex] = std::roundf(InOutShadowViewProjection.m[3][RowIndex] * EffectiveShadowMapSize) / EffectiveShadowMapSize;
        }

        InOutShadowViewProjection = DirectX::SimpleMath::Matrix::CreateScale(EffectiveShadowMapSize, EffectiveShadowMapSize, 1.0f) * InOutShadowViewProjection;
    }

    void BuildShadowCameraFromFrustumCorners(const std::array<DirectX::SimpleMath::Vector3, 8>& FrustumCorners, const DirectX::SimpleMath::Vector3& LightDirection, float ShadowMapSize, float ProjectionCoverageScale, DirectX::SimpleMath::Matrix& OutShadowView, DirectX::SimpleMath::Matrix& OutShadowProjection, DirectX::SimpleMath::Matrix& OutShadowViewProjection, DirectX::SimpleMath::Vector3& OutShadowCameraPosition, float& OutShadowNearPlane, float& OutShadowFarPlane, float& OutShadowAspectRatio) {
        DirectX::SimpleMath::Vector3 FrustumCenter{};
        for (const DirectX::SimpleMath::Vector3& FrustumCorner : FrustumCorners) {
            FrustumCenter += FrustumCorner;
        }

        FrustumCenter /= static_cast<float>(FrustumCorners.size());
        const DirectX::SimpleMath::Vector3 NormalizedLightDirection{ NormalizeDirection(LightDirection) };
        DirectX::SimpleMath::Vector3 UpDirection{ DirectX::SimpleMath::Vector3::Up };
        if (std::abs(DotProduct(NormalizedLightDirection, UpDirection)) >= ParallelDirectionThreshold) {
            UpDirection = DirectX::SimpleMath::Vector3::Right;
        }

        float FrustumRadius{ 0.0f };
        for (const DirectX::SimpleMath::Vector3& FrustumCorner : FrustumCorners) {
            const DirectX::SimpleMath::Vector3 CenterToCorner{ FrustumCorner - FrustumCenter };
            const float DistanceSquared{ DotProduct(CenterToCorner, CenterToCorner) };
            FrustumRadius = std::max(FrustumRadius, std::sqrtf(std::max(DistanceSquared, 0.0f)));
        }

        const float ShadowLightDistance{ std::max(FrustumRadius + ShadowCameraBackOffset, ShadowMinimumNearPlane + ShadowFarPlaneOffset + ShadowMinimumFarOffset) };
        OutShadowCameraPosition = FrustumCenter - (NormalizedLightDirection * ShadowLightDistance);
        OutShadowView = DirectX::SimpleMath::Matrix::CreateLookAt(OutShadowCameraPosition, FrustumCenter, UpDirection);

        float MinimumX{ std::numeric_limits<float>::max() };
        float MaximumX{ std::numeric_limits<float>::lowest() };
        float MinimumY{ std::numeric_limits<float>::max() };
        float MaximumY{ std::numeric_limits<float>::lowest() };
        float MinimumDepth{ std::numeric_limits<float>::max() };
        float MaximumDepth{ std::numeric_limits<float>::lowest() };

        for (const DirectX::SimpleMath::Vector3& FrustumCorner : FrustumCorners) {
            const DirectX::SimpleMath::Vector3 LightSpaceCorner{ DirectX::SimpleMath::Vector3::Transform(FrustumCorner, OutShadowView) };
            MinimumX = std::min(MinimumX, LightSpaceCorner.x);
            MaximumX = std::max(MaximumX, LightSpaceCorner.x);
            MinimumY = std::min(MinimumY, LightSpaceCorner.y);
            MaximumY = std::max(MaximumY, LightSpaceCorner.y);
            const float DepthFromLight{ DotProduct(FrustumCorner - OutShadowCameraPosition, NormalizedLightDirection) };
            MinimumDepth = std::min(MinimumDepth, DepthFromLight);
            MaximumDepth = std::max(MaximumDepth, DepthFromLight);
        }

        const float EffectiveProjectionCoverageScale{ std::max(ProjectionCoverageScale, 1.0f) };
        const float MinimumProjectionSpan{ std::max((FrustumRadius * 2.0f) * EffectiveProjectionCoverageScale, ShadowMinimumExtent) };
        const float ShadowProjectionWidth{ std::max(MaximumX - MinimumX, MinimumProjectionSpan) + ShadowProjectionSizeOffset };
        const float ShadowProjectionHeight{ std::max(MaximumY - MinimumY, MinimumProjectionSpan) + ShadowProjectionSizeOffset };
        const float ShadowProjectionHalfWidth{ ShadowProjectionWidth * 0.5f };
        const float ShadowProjectionHalfHeight{ ShadowProjectionHeight * 0.5f };
        float ProjectionCenterX{ (MinimumX + MaximumX) * 0.5f };
        float ProjectionCenterY{ (MinimumY + MaximumY) * 0.5f };
        const float ShadowMapResolution{ std::max(ShadowMapSize, 1.0f) };
        const float TexelSizeX{ ShadowProjectionWidth / ShadowMapResolution };
        const float TexelSizeY{ ShadowProjectionHeight / ShadowMapResolution };
        ProjectionCenterX = SnapToGrid(ProjectionCenterX, TexelSizeX);
        ProjectionCenterY = SnapToGrid(ProjectionCenterY, TexelSizeY);

        const float ProjectionLeft{ ProjectionCenterX - ShadowProjectionHalfWidth };
        const float ProjectionRight{ ProjectionCenterX + ShadowProjectionHalfWidth };
        const float ProjectionBottom{ ProjectionCenterY - ShadowProjectionHalfHeight };
        const float ProjectionTop{ ProjectionCenterY + ShadowProjectionHalfHeight };
        OutShadowNearPlane = std::max(ShadowMinimumNearPlane, MinimumDepth - ShadowNearPlaneOffset);
        OutShadowFarPlane = std::max(OutShadowNearPlane + ShadowMinimumFarOffset, MaximumDepth + ShadowFarPlaneOffset);
        OutShadowAspectRatio = ShadowProjectionWidth / std::max(ShadowProjectionHeight, ShadowMinimumExtent);
        OutShadowProjection = DirectX::SimpleMath::Matrix::CreateOrthographicOffCenter(ProjectionLeft, ProjectionRight, ProjectionBottom, ProjectionTop, OutShadowNearPlane, OutShadowFarPlane);
        OutShadowViewProjection = OutShadowView * OutShadowProjection;
        if (ShadowViewProjectionStabilizationEnabled == true) {
            StabilizeShadowViewProjection(OutShadowViewProjection, ShadowMapSize);
        }
    }
}

namespace Game {
    const std::string& ShadowMappingParameterSystem::Name() const {
        return mName;
    }

    Phase ShadowMappingParameterSystem::GetPhase() const {
        return Phase::RenderPrepare;
    }

    std::span<const ComponentAccess> ShadowMappingParameterSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 2> Accesses{ { { typeid(Transform), Access::Read }, { typeid(Camera), Access::Read } } };
        return Accesses;
    }

    std::span<const ResourceAccess> ShadowMappingParameterSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 1> Accesses{ { { typeid(RFD::RenderFrameData), Access::Write } } };
        return Accesses;
    }

    void ShadowMappingParameterSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        for (auto [TransformComponent, CameraComponent] : World.Query<Transform, Camera>()) {
            if (CameraComponent.isActive == false) {
                continue;
            }

            const RFD::ShadowMappingParameter ShadowMappingParameter{ BuildShadowMappingParameter(CameraComponent, TransformComponent) };
            Ctx.RenderData.shadowMapping = ShadowMappingParameter;
            break;
        }
    }

    RFD::ShadowMappingParameter ShadowMappingParameterSystem::BuildShadowMappingParameter(const Camera& CameraComponent, const Transform& TransformComponent) const {
        RFD::ShadowMappingParameter Parameter{};
        DirectX::SimpleMath::Vector3 NormalizedLightDirection{ ShadowLightDirection };
        NormalizedLightDirection.Normalize();
        const ShadowCascadeRange ClosestCascadeRange{ ComputeCascadeRange(CameraComponent, ShadowClosestCascadeIndex, ShadowCascadeCount, ShadowCascadeSplitLambda) };
        const std::array<DirectX::SimpleMath::Vector3, 8> FrustumCorners{ BuildCameraFrustumCorners(CameraComponent, TransformComponent, ClosestCascadeRange) };
        DirectX::SimpleMath::Matrix ShadowView{};
        DirectX::SimpleMath::Matrix ShadowProjection{};
        DirectX::SimpleMath::Matrix ShadowViewProjection{};
        DirectX::SimpleMath::Vector3 ShadowCameraPosition{};
        float ShadowNearPlane{ 0.0f };
        float ShadowFarPlane{ 0.0f };
        float ShadowAspectRatio{ 1.0f };
        BuildShadowCameraFromFrustumCorners(FrustumCorners, NormalizedLightDirection, Parameter.shadowMapSize, ShadowClosestCascadeProjectionCoverageScale, ShadowView, ShadowProjection, ShadowViewProjection, ShadowCameraPosition, ShadowNearPlane, ShadowFarPlane, ShadowAspectRatio);

        Parameter.shadowCamera.view = ShadowView;
        Parameter.shadowCamera.proj = ShadowProjection;
        Parameter.shadowCamera.viewProj = ShadowViewProjection;
        Parameter.shadowCamera.position = DirectX::SimpleMath::Vector4{ ShadowCameraPosition.x, ShadowCameraPosition.y, ShadowCameraPosition.z, 1.0f };
        Parameter.shadowCamera.nearPlane = ShadowNearPlane;
        Parameter.shadowCamera.farPlane = ShadowFarPlane;
        Parameter.shadowCamera.aspectRatio = ShadowAspectRatio;
        Parameter.shadowCamera.fovRadians = 0.0f;
        Parameter.lightDirection = DirectX::SimpleMath::Vector4{ NormalizedLightDirection.x, NormalizedLightDirection.y, NormalizedLightDirection.z, 0.0f };

        return Parameter;
    }
}
