#include "ShadowMappingParameterSystem.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include "Core/Config.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/DirectionalLight.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Transform.h"
#include "RenderContract/Writer/FrameRenderWriter.h"


#undef min
#undef max 

#include <ryml.hpp>
#include <ryml_std.hpp>

namespace {
    struct ShadowCascadeRange {
        float nearPlane{ 0.0f };
        float farPlane{ 0.0f };
    };

    float DotProduct(const DirectX::SimpleMath::Vector3& Left, const DirectX::SimpleMath::Vector3& Right) {
        return (Left.x * Right.x) + (Left.y * Right.y) + (Left.z * Right.z);
    }

    void LoadFloatParameter(const c4::yml::ConstNodeRef& Node, const char* Key, float& OutValue) {
        if (Node.has_child(Key)) {
            Node[Key] >> OutValue;
        }
    }

    void LoadUintParameter(const c4::yml::ConstNodeRef& Node, const char* Key, std::uint32_t& OutValue) {
        if (Node.has_child(Key)) {
            Node[Key] >> OutValue;
        }
    }

    void LoadIntParameter(const c4::yml::ConstNodeRef& Node, const char* Key, std::int32_t& OutValue) {
        if (Node.has_child(Key)) {
            Node[Key] >> OutValue;
        }
    }

    void LoadBoolParameter(const c4::yml::ConstNodeRef& Node, const char* Key, bool& OutValue) {
        if (Node.has_child(Key)) {
            Node[Key] >> OutValue;
        }
    }

    void LoadVector3Parameter(const c4::yml::ConstNodeRef& Node, const char* Key, DirectX::SimpleMath::Vector3& OutValue) {
        if (Node.has_child(Key) == false) {
            return;
        }

        const c4::yml::ConstNodeRef VectorNode{ Node[Key] };
        LoadFloatParameter(VectorNode, "X", OutValue.x);
        LoadFloatParameter(VectorNode, "Y", OutValue.y);
        LoadFloatParameter(VectorNode, "Z", OutValue.z);
    }

    void LoadVector4Parameter(const c4::yml::ConstNodeRef& Node, const char* Key, DirectX::SimpleMath::Vector4& OutValue) {
        if (Node.has_child(Key) == false) {
            return;
        }

        const c4::yml::ConstNodeRef VectorNode{ Node[Key] };
        LoadFloatParameter(VectorNode, "X", OutValue.x);
        LoadFloatParameter(VectorNode, "Y", OutValue.y);
        LoadFloatParameter(VectorNode, "Z", OutValue.z);
        LoadFloatParameter(VectorNode, "W", OutValue.w);
    }

    void SaveVector3Parameter(std::ofstream& OutputStream, const char* Name, const DirectX::SimpleMath::Vector3& Value) {
        OutputStream << "    " << Name << ":\n";
        OutputStream << "      X: " << Value.x << "\n";
        OutputStream << "      Y: " << Value.y << "\n";
        OutputStream << "      Z: " << Value.z << "\n";
    }

    void SaveVector4Parameter(std::ofstream& OutputStream, const char* Name, const DirectX::SimpleMath::Vector4& Value) {
        OutputStream << "    " << Name << ":\n";
        OutputStream << "      X: " << Value.x << "\n";
        OutputStream << "      Y: " << Value.y << "\n";
        OutputStream << "      Z: " << Value.z << "\n";
        OutputStream << "      W: " << Value.w << "\n";
    }

    float SnapToGrid(float Value, float GridSize, float MinimumGridSize) {
        const float SafeGridSize{ std::max(GridSize, MinimumGridSize) };
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

    RenderContract::DirectionalLightParameter BuildDefaultDirectionalLightParameter(const DirectX::SimpleMath::Vector3& DefaultLightDirection, const DirectX::SimpleMath::Vector4& DefaultLightColor, float DefaultLightIntensity, float DefaultAmbientLightIntensity, bool DefaultLightCastsShadow) {
        const DirectX::SimpleMath::Vector3 NormalizedLightDirection{ NormalizeDirection(DefaultLightDirection) };
        RenderContract::DirectionalLightParameter Parameter{};
        Parameter.mDirection = DirectX::SimpleMath::Vector4{ NormalizedLightDirection.x, NormalizedLightDirection.y, NormalizedLightDirection.z, 0.0f };
        Parameter.mColor = DefaultLightColor;
        Parameter.mIntensity = std::max(DefaultLightIntensity, 0.0f);
        Parameter.mAmbientIntensity = std::max(DefaultAmbientLightIntensity, 0.0f);
        Parameter.mFlags = RenderContract::DirectionalLightParameterFlagActive;
        if (DefaultLightCastsShadow == true) {
            Parameter.mFlags |= RenderContract::DirectionalLightParameterFlagCastShadow;
        }

        Parameter.mPadding0 = 0.0f;
        return Parameter;
    }

    DirectX::SimpleMath::Vector3 ResolveDirectionalLightDirection(const Game::DirectionalLight& LightComponent, const Game::Transform* TransformComponent) {
        if (LightComponent.mUseTransformDirection == true && TransformComponent != nullptr) {
            return TransformComponent->GetForwardDirection();
        }

        return LightComponent.mDirection;
    }

    RenderContract::DirectionalLightParameter BuildDirectionalLightParameterFromComponent(const Game::DirectionalLight& LightComponent, const Game::Transform* TransformComponent) {
        const DirectX::SimpleMath::Vector3 NormalizedLightDirection{ NormalizeDirection(ResolveDirectionalLightDirection(LightComponent, TransformComponent)) };
        RenderContract::DirectionalLightParameter Parameter{};
        Parameter.mDirection = DirectX::SimpleMath::Vector4{ NormalizedLightDirection.x, NormalizedLightDirection.y, NormalizedLightDirection.z, 0.0f };
        Parameter.mColor = DirectX::SimpleMath::Vector4{ LightComponent.mColor.x, LightComponent.mColor.y, LightComponent.mColor.z, 1.0f };
        Parameter.mIntensity = std::max(LightComponent.mIntensity, 0.0f);
        Parameter.mAmbientIntensity = std::max(LightComponent.mAmbientIntensity, 0.0f);
        Parameter.mFlags = 0u;
        if (LightComponent.mIsActive == true) {
            Parameter.mFlags |= RenderContract::DirectionalLightParameterFlagActive;
        }

        if (LightComponent.mIsActive == true && LightComponent.mCastsShadow == true) {
            Parameter.mFlags |= RenderContract::DirectionalLightParameterFlagCastShadow;
        }

        Parameter.mPadding0 = 0.0f;
        return Parameter;
    }

    float ComputeCascadeSplitDistance(float CameraNearPlane, float CameraFarPlane, int CascadeIndex, int CascadeCount, float CascadeSplitLambda, float MinimumNearPlane, float MinimumViewDistance, float FirstCascadeCoverageDistance, float SecondCascadeCoverageScale) {
        const float EffectiveNearPlane{ std::max(CameraNearPlane, MinimumNearPlane) };
        const float EffectiveFarPlane{ std::max(CameraFarPlane, EffectiveNearPlane + MinimumViewDistance) };
        const int EffectiveCascadeCount{ std::max(CascadeCount, 1) };
        if (EffectiveCascadeCount == 2) {
            if (CascadeIndex <= 0) {
                return EffectiveNearPlane;
            }

            if (CascadeIndex >= EffectiveCascadeCount) {
                const float SecondCascadeFarPlane{ EffectiveNearPlane + (FirstCascadeCoverageDistance * SecondCascadeCoverageScale) };
                return std::min(SecondCascadeFarPlane, EffectiveFarPlane);
            }

            const float FirstCascadeFarPlane{ std::min(EffectiveNearPlane + FirstCascadeCoverageDistance, EffectiveFarPlane) };
            return FirstCascadeFarPlane;
        }

        const float SplitRatio{ static_cast<float>(CascadeIndex) / static_cast<float>(EffectiveCascadeCount) };
        const float UniformSplitDistance{ EffectiveNearPlane + ((EffectiveFarPlane - EffectiveNearPlane) * SplitRatio) };
        const float LogarithmicSplitDistance{ EffectiveNearPlane * std::pow(EffectiveFarPlane / EffectiveNearPlane, SplitRatio) };
        const float EffectiveCascadeSplitLambda{ std::clamp(CascadeSplitLambda, 0.0f, 1.0f) };
        return std::lerp(UniformSplitDistance, LogarithmicSplitDistance, EffectiveCascadeSplitLambda);
    }

    ShadowCascadeRange ComputeCascadeRange(const Game::Camera& CameraComponent, int CascadeIndex, int CascadeCount, float CascadeSplitLambda, float CascadeMaximumDistance, float CascadeNearRangeExpansionDistance, int CascadeExpandedBoundaryCount, float MinimumNearPlane, float MinimumViewDistance, float FirstCascadeCoverageDistance, float SecondCascadeCoverageScale) {
        const float EffectiveNearPlane{ std::max(CameraComponent.nearPlane, MinimumNearPlane) };
        const float MinimumFarPlane{ EffectiveNearPlane + MinimumViewDistance };
        const float MaximumFarPlane{ std::max(CascadeMaximumDistance, MinimumFarPlane) };
        const float EffectiveFarPlane{ std::clamp(CameraComponent.farPlane, MinimumFarPlane, MaximumFarPlane) };
        const int EffectiveCascadeIndex{ std::clamp(CascadeIndex, 0, std::max(CascadeCount - 1, 0)) };
        const float CascadeNearPlaneBase{ ComputeCascadeSplitDistance(EffectiveNearPlane, EffectiveFarPlane, EffectiveCascadeIndex, CascadeCount, CascadeSplitLambda, MinimumNearPlane, MinimumViewDistance, FirstCascadeCoverageDistance, SecondCascadeCoverageScale) };
        const float CascadeFarPlaneBase{ ComputeCascadeSplitDistance(EffectiveNearPlane, EffectiveFarPlane, EffectiveCascadeIndex + 1, CascadeCount, CascadeSplitLambda, MinimumNearPlane, MinimumViewDistance, FirstCascadeCoverageDistance, SecondCascadeCoverageScale) };
        const int CascadeNearBoundaryExpansionStepCount{ std::clamp(EffectiveCascadeIndex, 0, CascadeExpandedBoundaryCount) };
        const int CascadeFarBoundaryExpansionStepCount{ std::clamp(EffectiveCascadeIndex + 1, 0, CascadeExpandedBoundaryCount) };
        const float CascadeNearPlane{ std::clamp(CascadeNearPlaneBase + (static_cast<float>(CascadeNearBoundaryExpansionStepCount) * CascadeNearRangeExpansionDistance), EffectiveNearPlane, EffectiveFarPlane) };
        const float CascadeFarPlane{ std::clamp(CascadeFarPlaneBase + (static_cast<float>(CascadeFarBoundaryExpansionStepCount) * CascadeNearRangeExpansionDistance), CascadeNearPlane, EffectiveFarPlane) };
        ShadowCascadeRange CascadeRange{};
        CascadeRange.nearPlane = CascadeNearPlane;
        CascadeRange.farPlane = std::max(CascadeFarPlane, CascadeNearPlane + MinimumViewDistance);
        return CascadeRange;
    }

    std::array<DirectX::SimpleMath::Vector3, 8> BuildCameraFrustumCorners(const Game::Camera& CameraComponent, const Game::Transform& TransformComponent, const ShadowCascadeRange& CascadeRange, float MinimumNearPlane, float MinimumExtent, float MinimumViewDistance, float MinimumAspectRatio, float MinimumFovDegrees, float MaximumFovDegrees, float ClipWMinimum) {
        (void)TransformComponent;

        const float EffectiveAspectRatio{ std::max(CameraComponent.aspectRatio, MinimumAspectRatio) };
        const float EffectiveNearPlane{ std::max(CascadeRange.nearPlane, MinimumNearPlane) };
        const float EffectiveFarPlane{ std::max(CascadeRange.farPlane, EffectiveNearPlane + MinimumViewDistance) };

        DirectX::SimpleMath::Matrix EffectiveProjection{};
        if (CameraComponent.isOrthographic) {
            const float OrthographicHeight{ std::max(CameraComponent.orthoSize * 2.0f, MinimumExtent) };
            const float OrthographicWidth{ std::max(OrthographicHeight * EffectiveAspectRatio, MinimumExtent) };
            EffectiveProjection = DirectX::SimpleMath::Matrix::CreateOrthographic(OrthographicWidth, OrthographicHeight, EffectiveNearPlane, EffectiveFarPlane);
        }
        else {
            float FovRadians{ DirectX::XMConvertToRadians(CameraComponent.fov) };
            const float MinimumFovRadians{ DirectX::XMConvertToRadians(MinimumFovDegrees) };
            const float MaximumFovRadians{ DirectX::XMConvertToRadians(MaximumFovDegrees) };
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
            const float WorldW{ std::abs(WorldCorner.w) <= ClipWMinimum ? 1.0f : WorldCorner.w };
            FrustumCorners[Index] = DirectX::SimpleMath::Vector3{ WorldCorner.x / WorldW, WorldCorner.y / WorldW, WorldCorner.z / WorldW };
        }

        return FrustumCorners;
    }

    void StabilizeShadowViewProjection(DirectX::SimpleMath::Matrix& InOutShadowViewProjection, float ShadowMapSize, float MinimumShadowMapSize) {
        const float EffectiveShadowMapSize{ std::max(ShadowMapSize, MinimumShadowMapSize) };
        const float InverseShadowMapSize{ 1.0f / EffectiveShadowMapSize };
        InOutShadowViewProjection = DirectX::SimpleMath::Matrix::CreateScale(InverseShadowMapSize, InverseShadowMapSize, 1.0f) * InOutShadowViewProjection;

        for (int RowIndex{ 0 }; RowIndex < 3; RowIndex += 1) {
            InOutShadowViewProjection.m[3][RowIndex] = std::roundf(InOutShadowViewProjection.m[3][RowIndex] * EffectiveShadowMapSize) / EffectiveShadowMapSize;
        }

        InOutShadowViewProjection = DirectX::SimpleMath::Matrix::CreateScale(EffectiveShadowMapSize, EffectiveShadowMapSize, 1.0f) * InOutShadowViewProjection;
    }

    void BuildShadowCameraFromFrustumCorners(const std::array<DirectX::SimpleMath::Vector3, 8>& FrustumCorners, const DirectX::SimpleMath::Vector3& LightDirection, float ShadowMapSize, float ProjectionCoverageScale, float MinimumExtent, float MinimumNearPlane, float MinimumFarOffset, float NearPlaneOffset, float FarPlaneOffset, float ProjectionSizeOffset, float CameraBackOffset, float CasterDepthExpansionScale, float ParallelDirectionThreshold, float MinimumGridSize, float MinimumShadowMapSize, bool ViewProjectionStabilizationEnabled, DirectX::SimpleMath::Matrix& OutShadowView, DirectX::SimpleMath::Matrix& OutShadowProjection, DirectX::SimpleMath::Matrix& OutShadowViewProjection, DirectX::SimpleMath::Vector3& OutShadowCameraPosition, float& OutShadowNearPlane, float& OutShadowFarPlane, float& OutShadowAspectRatio) {
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

        const float ShadowCasterDepthExpansionDistance{ std::max(FrustumRadius * CasterDepthExpansionScale, CameraBackOffset) };
        const float ShadowLightDistance{ std::max(FrustumRadius + ShadowCasterDepthExpansionDistance, MinimumNearPlane + FarPlaneOffset + MinimumFarOffset) };
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
        const float MinimumProjectionSpan{ std::max((FrustumRadius * 2.0f) * EffectiveProjectionCoverageScale, MinimumExtent) };
        const float ShadowProjectionWidth{ std::max(MaximumX - MinimumX, MinimumProjectionSpan) + ProjectionSizeOffset };
        const float ShadowProjectionHeight{ std::max(MaximumY - MinimumY, MinimumProjectionSpan) + ProjectionSizeOffset };
        const float ShadowProjectionHalfWidth{ ShadowProjectionWidth * 0.5f };
        const float ShadowProjectionHalfHeight{ ShadowProjectionHeight * 0.5f };
        float ProjectionCenterX{ (MinimumX + MaximumX) * 0.5f };
        float ProjectionCenterY{ (MinimumY + MaximumY) * 0.5f };
        const float ShadowMapResolution{ std::max(ShadowMapSize, MinimumShadowMapSize) };
        const float TexelSizeX{ ShadowProjectionWidth / ShadowMapResolution };
        const float TexelSizeY{ ShadowProjectionHeight / ShadowMapResolution };
        ProjectionCenterX = SnapToGrid(ProjectionCenterX, TexelSizeX, MinimumGridSize);
        ProjectionCenterY = SnapToGrid(ProjectionCenterY, TexelSizeY, MinimumGridSize);

        const float ProjectionLeft{ ProjectionCenterX - ShadowProjectionHalfWidth };
        const float ProjectionRight{ ProjectionCenterX + ShadowProjectionHalfWidth };
        const float ProjectionBottom{ ProjectionCenterY - ShadowProjectionHalfHeight };
        const float ProjectionTop{ ProjectionCenterY + ShadowProjectionHalfHeight };
        OutShadowNearPlane = std::max(MinimumNearPlane, MinimumDepth - ShadowCasterDepthExpansionDistance - NearPlaneOffset);
        OutShadowFarPlane = std::max(OutShadowNearPlane + MinimumFarOffset, MaximumDepth + FarPlaneOffset);
        OutShadowAspectRatio = ShadowProjectionWidth / std::max(ShadowProjectionHeight, MinimumExtent);
        OutShadowProjection = DirectX::SimpleMath::Matrix::CreateOrthographicOffCenter(ProjectionLeft, ProjectionRight, ProjectionBottom, ProjectionTop, OutShadowNearPlane, OutShadowFarPlane);
        OutShadowViewProjection = OutShadowView * OutShadowProjection;
        if (ViewProjectionStabilizationEnabled == true) {
            StabilizeShadowViewProjection(OutShadowViewProjection, ShadowMapSize, MinimumShadowMapSize);
        }
    }

    void SetCascadeSplitDistance(DirectX::SimpleMath::Vector4& InOutCascadeSplitDistances, int CascadeIndex, float CascadeSplitDistance) {
        if (CascadeIndex == 0) {
            InOutCascadeSplitDistances.x = CascadeSplitDistance;
        }
        else if (CascadeIndex == 1) {
            InOutCascadeSplitDistances.y = CascadeSplitDistance;
        }
        else if (CascadeIndex == 2) {
            InOutCascadeSplitDistances.z = CascadeSplitDistance;
        }
        else if (CascadeIndex == 3) {
            InOutCascadeSplitDistances.w = CascadeSplitDistance;
        }
    }
}

namespace Game {
    ShadowMappingParameterSystem::ShadowMappingParameterSystem() {
        LoadShadowMappingParameterFile();
    }

    void ShadowMappingParameterSystem::LoadShadowMappingParameterFile() {
        std::ifstream InputStream{ mShadowMappingParameterFilePath, std::ios::in | std::ios::binary };
        if (InputStream.is_open() == false) {
            SanitizeShadowMappingParameters();
            SaveShadowMappingParameterFile();
            return;
        }

        std::stringstream Buffer{};
        Buffer << InputStream.rdbuf();
        const std::string YamlText{ Buffer.str() };
        if (YamlText.empty() == true) {
            SanitizeShadowMappingParameters();
            SaveShadowMappingParameterFile();
            return;
        }

        try {
            c4::yml::Tree Tree{};
            c4::yml::parse_in_arena(c4::to_csubstr(YamlText), &Tree);
            const c4::yml::ConstNodeRef RootNode{ Tree.rootref() };
            const c4::yml::ConstNodeRef ShadowMappingNode{ RootNode.has_child("ShadowMapping") ? RootNode["ShadowMapping"] : RootNode };

            if (ShadowMappingNode.invalid() == false) {
                LoadUintParameter(ShadowMappingNode, "CascadeCount", mCascadeCount);
                LoadFloatParameter(ShadowMappingNode, "CascadeMaximumDistance", mCascadeMaximumDistance);
                LoadFloatParameter(ShadowMappingNode, "CascadeSplitLambda", mCascadeSplitLambda);
                LoadFloatParameter(ShadowMappingNode, "CascadeNearRangeExpansionDistance", mCascadeNearRangeExpansionDistance);
                LoadIntParameter(ShadowMappingNode, "CascadeExpandedBoundaryCount", mCascadeExpandedBoundaryCount);
                LoadFloatParameter(ShadowMappingNode, "FirstCascadeCoverageDistance", mFirstCascadeCoverageDistance);
                LoadFloatParameter(ShadowMappingNode, "SecondCascadeCoverageScale", mSecondCascadeCoverageScale);
                LoadFloatParameter(ShadowMappingNode, "MinimumNearPlane", mMinimumNearPlane);
                LoadFloatParameter(ShadowMappingNode, "MinimumExtent", mMinimumExtent);
                LoadFloatParameter(ShadowMappingNode, "MinimumViewDistance", mMinimumViewDistance);
                LoadFloatParameter(ShadowMappingNode, "MinimumFarOffset", mMinimumFarOffset);
                LoadFloatParameter(ShadowMappingNode, "NearPlaneOffset", mNearPlaneOffset);
                LoadFloatParameter(ShadowMappingNode, "FarPlaneOffset", mFarPlaneOffset);
                LoadFloatParameter(ShadowMappingNode, "ProjectionSizeOffset", mProjectionSizeOffset);
                LoadFloatParameter(ShadowMappingNode, "CameraBackOffset", mCameraBackOffset);
                LoadFloatParameter(ShadowMappingNode, "CasterDepthExpansionScale", mCasterDepthExpansionScale);
                LoadFloatParameter(ShadowMappingNode, "ProjectionCoverageScale", mProjectionCoverageScale);
                LoadFloatParameter(ShadowMappingNode, "ParallelDirectionThreshold", mParallelDirectionThreshold);
                LoadFloatParameter(ShadowMappingNode, "MinimumGridSize", mMinimumGridSize);
                LoadFloatParameter(ShadowMappingNode, "MinimumAspectRatio", mMinimumAspectRatio);
                LoadFloatParameter(ShadowMappingNode, "MinimumFovDegrees", mMinimumFovDegrees);
                LoadFloatParameter(ShadowMappingNode, "MaximumFovDegrees", mMaximumFovDegrees);
                LoadFloatParameter(ShadowMappingNode, "ClipWMinimum", mClipWMinimum);
                LoadFloatParameter(ShadowMappingNode, "MinimumShadowMapSize", mMinimumShadowMapSize);
                LoadFloatParameter(ShadowMappingNode, "MinimumProjectionDivisor", mMinimumProjectionDivisor);
                LoadFloatParameter(ShadowMappingNode, "MinimumProjectionDepthSpan", mMinimumProjectionDepthSpan);
                LoadBoolParameter(ShadowMappingNode, "ViewProjectionStabilizationEnabled", mViewProjectionStabilizationEnabled);
                if (ShadowMappingNode.has_child("DefaultDirectionalLight")) {
                    const c4::yml::ConstNodeRef DefaultLightNode{ ShadowMappingNode["DefaultDirectionalLight"] };
                    LoadVector3Parameter(DefaultLightNode, "Direction", mDefaultDirectionalLightDirection);
                    LoadVector4Parameter(DefaultLightNode, "Color", mDefaultDirectionalLightColor);
                    LoadFloatParameter(DefaultLightNode, "Intensity", mDefaultDirectionalLightIntensity);
                    LoadFloatParameter(DefaultLightNode, "AmbientIntensity", mDefaultAmbientLightIntensity);
                    LoadBoolParameter(DefaultLightNode, "CastsShadow", mDefaultDirectionalLightCastsShadow);
                }

                bool IsCascadeParameterLoaded{ false };
                if (ShadowMappingNode.has_child("Cascades")) {
                    const c4::yml::ConstNodeRef CascadeNodes{ ShadowMappingNode["Cascades"] };
                    if (CascadeNodes.invalid() == false && CascadeNodes.is_seq() == true) {
                        std::size_t CascadeIndex{ 0 };
                        for (const c4::yml::ConstNodeRef CascadeNode : CascadeNodes.children()) {
                            if (CascadeIndex >= RenderContract::ShadowCascadeMaxCount) {
                                break;
                            }

                            if (CascadeNode.has_child("ShadowMapSize")) {
                                CascadeNode["ShadowMapSize"] >> mShadowMapSizes[CascadeIndex];
                            }

                            if (CascadeNode.has_child("ShadowBias")) {
                                CascadeNode["ShadowBias"] >> mShadowBiases[CascadeIndex];
                            }

                            if (CascadeNode.has_child("ShadowStrength")) {
                                CascadeNode["ShadowStrength"] >> mShadowStrengths[CascadeIndex];
                            }

                            if (CascadeNode.has_child("RasterDepthBias")) {
                                CascadeNode["RasterDepthBias"] >> mRasterDepthBiases[CascadeIndex];
                            }

                            if (CascadeNode.has_child("RasterSlopeScaledDepthBias")) {
                                CascadeNode["RasterSlopeScaledDepthBias"] >> mRasterSlopeScaledDepthBiases[CascadeIndex];
                            }

                            CascadeIndex += 1;
                        }

                        IsCascadeParameterLoaded = CascadeIndex > 0;
                    }
                }

                if (IsCascadeParameterLoaded == false) {
                    float LegacyShadowMapSize{ mShadowMapSizes[1] };
                    if (ShadowMappingNode.has_child("ShadowMapSize")) {
                        ShadowMappingNode["ShadowMapSize"] >> LegacyShadowMapSize;
                    }

                    float LegacyShadowBias{ mShadowBiases[0] };
                    if (ShadowMappingNode.has_child("ShadowBias")) {
                        ShadowMappingNode["ShadowBias"] >> LegacyShadowBias;
                    }

                    float LegacyShadowStrength{ mShadowStrengths[0] };
                    if (ShadowMappingNode.has_child("ShadowStrength")) {
                        ShadowMappingNode["ShadowStrength"] >> LegacyShadowStrength;
                    }

                    float LegacyRasterDepthBias{ mRasterDepthBiases[0] };
                    if (ShadowMappingNode.has_child("RasterDepthBias")) {
                        ShadowMappingNode["RasterDepthBias"] >> LegacyRasterDepthBias;
                    }

                    float LegacyRasterSlopeScaledDepthBias{ mRasterSlopeScaledDepthBiases[0] };
                    if (ShadowMappingNode.has_child("RasterSlopeScaledDepthBias")) {
                        ShadowMappingNode["RasterSlopeScaledDepthBias"] >> LegacyRasterSlopeScaledDepthBias;
                    }

                    for (std::size_t CascadeIndex{ 0 }; CascadeIndex < RenderContract::ShadowCascadeMaxCount; CascadeIndex += 1) {
                        mShadowMapSizes[CascadeIndex] = LegacyShadowMapSize;
                        mShadowBiases[CascadeIndex] = LegacyShadowBias;
                        mShadowStrengths[CascadeIndex] = LegacyShadowStrength;
                        mRasterDepthBiases[CascadeIndex] = LegacyRasterDepthBias;
                        mRasterSlopeScaledDepthBiases[CascadeIndex] = LegacyRasterSlopeScaledDepthBias;
                    }
                }

            }
        }
        catch (...) {
        }

        SanitizeShadowMappingParameters();
        SaveShadowMappingParameterFile();
    }

    void ShadowMappingParameterSystem::SaveShadowMappingParameterFile() const {
        std::error_code ErrorCode{};
        const std::filesystem::path ParentPath{ mShadowMappingParameterFilePath.parent_path() };
        if (ParentPath.empty() == false) {
            std::filesystem::create_directories(ParentPath, ErrorCode);
        }

        std::ofstream OutputStream{ mShadowMappingParameterFilePath, std::ios::out | std::ios::binary | std::ios::trunc };
        if (OutputStream.is_open() == false) {
            return;
        }

        OutputStream << "ShadowMapping:\n";
        OutputStream << "  CascadeCount: " << mCascadeCount << "\n";
        OutputStream << "  Cascades:\n";
        const std::size_t ActiveCascadeCount{ static_cast<std::size_t>(std::min(mCascadeCount, RenderContract::ShadowCascadeMaxCount)) };
        for (std::size_t CascadeIndex{ 0 }; CascadeIndex < ActiveCascadeCount; CascadeIndex += 1) {
            OutputStream << "    - ShadowMapSize: " << mShadowMapSizes[CascadeIndex] << "\n";
            OutputStream << "      ShadowBias: " << mShadowBiases[CascadeIndex] << "\n";
            OutputStream << "      ShadowStrength: " << mShadowStrengths[CascadeIndex] << "\n";
            OutputStream << "      RasterDepthBias: " << mRasterDepthBiases[CascadeIndex] << "\n";
            OutputStream << "      RasterSlopeScaledDepthBias: " << mRasterSlopeScaledDepthBiases[CascadeIndex] << "\n";
        }

        OutputStream << "  CascadeMaximumDistance: " << mCascadeMaximumDistance << "\n";
        OutputStream << "  CascadeSplitLambda: " << mCascadeSplitLambda << "\n";
        OutputStream << "  CascadeNearRangeExpansionDistance: " << mCascadeNearRangeExpansionDistance << "\n";
        OutputStream << "  CascadeExpandedBoundaryCount: " << mCascadeExpandedBoundaryCount << "\n";
        OutputStream << "  FirstCascadeCoverageDistance: " << mFirstCascadeCoverageDistance << "\n";
        OutputStream << "  SecondCascadeCoverageScale: " << mSecondCascadeCoverageScale << "\n";
        OutputStream << "  MinimumNearPlane: " << mMinimumNearPlane << "\n";
        OutputStream << "  MinimumExtent: " << mMinimumExtent << "\n";
        OutputStream << "  MinimumViewDistance: " << mMinimumViewDistance << "\n";
        OutputStream << "  MinimumFarOffset: " << mMinimumFarOffset << "\n";
        OutputStream << "  NearPlaneOffset: " << mNearPlaneOffset << "\n";
        OutputStream << "  FarPlaneOffset: " << mFarPlaneOffset << "\n";
        OutputStream << "  ProjectionSizeOffset: " << mProjectionSizeOffset << "\n";
        OutputStream << "  CameraBackOffset: " << mCameraBackOffset << "\n";
        OutputStream << "  CasterDepthExpansionScale: " << mCasterDepthExpansionScale << "\n";
        OutputStream << "  ProjectionCoverageScale: " << mProjectionCoverageScale << "\n";
        OutputStream << "  ParallelDirectionThreshold: " << mParallelDirectionThreshold << "\n";
        OutputStream << "  MinimumGridSize: " << mMinimumGridSize << "\n";
        OutputStream << "  MinimumAspectRatio: " << mMinimumAspectRatio << "\n";
        OutputStream << "  MinimumFovDegrees: " << mMinimumFovDegrees << "\n";
        OutputStream << "  MaximumFovDegrees: " << mMaximumFovDegrees << "\n";
        OutputStream << "  ClipWMinimum: " << mClipWMinimum << "\n";
        OutputStream << "  MinimumShadowMapSize: " << mMinimumShadowMapSize << "\n";
        OutputStream << "  MinimumProjectionDivisor: " << mMinimumProjectionDivisor << "\n";
        OutputStream << "  MinimumProjectionDepthSpan: " << mMinimumProjectionDepthSpan << "\n";
        OutputStream << "  ViewProjectionStabilizationEnabled: " << (mViewProjectionStabilizationEnabled == true ? "true" : "false") << "\n";
        OutputStream << "  DefaultDirectionalLight:\n";
        SaveVector3Parameter(OutputStream, "Direction", mDefaultDirectionalLightDirection);
        SaveVector4Parameter(OutputStream, "Color", mDefaultDirectionalLightColor);
        OutputStream << "    Intensity: " << mDefaultDirectionalLightIntensity << "\n";
        OutputStream << "    AmbientIntensity: " << mDefaultAmbientLightIntensity << "\n";
        OutputStream << "    CastsShadow: " << (mDefaultDirectionalLightCastsShadow == true ? "true" : "false") << "\n";
    }

    void ShadowMappingParameterSystem::SanitizeShadowMappingParameters() {
        mCascadeCount = std::max<std::uint32_t>(1u, std::min<std::uint32_t>(mCascadeCount, RenderContract::ShadowCascadeMaxCount));
        mMinimumShadowMapSize = std::max(mMinimumShadowMapSize, 1.0f);
        const std::size_t ActiveCascadeCount{ static_cast<std::size_t>(mCascadeCount) };
        for (std::size_t CascadeIndex{ 0 }; CascadeIndex < ActiveCascadeCount; CascadeIndex += 1) {
            mShadowMapSizes[CascadeIndex] = std::max(mShadowMapSizes[CascadeIndex], mMinimumShadowMapSize);
            mShadowBiases[CascadeIndex] = std::max(mShadowBiases[CascadeIndex], 0.0f);
            mShadowStrengths[CascadeIndex] = std::clamp(mShadowStrengths[CascadeIndex], 0.0f, 1.0f);
            mRasterDepthBiases[CascadeIndex] = std::max(mRasterDepthBiases[CascadeIndex], 0.0f);
            mRasterSlopeScaledDepthBiases[CascadeIndex] = std::max(mRasterSlopeScaledDepthBiases[CascadeIndex], 0.0f);
        }

        mMinimumNearPlane = std::max(mMinimumNearPlane, 0.0f);
        mMinimumExtent = std::max(mMinimumExtent, 0.0f);
        mMinimumViewDistance = std::max(mMinimumViewDistance, 0.0f);
        mMinimumFarOffset = std::max(mMinimumFarOffset, 0.0f);
        mNearPlaneOffset = std::max(mNearPlaneOffset, 0.0f);
        mFarPlaneOffset = std::max(mFarPlaneOffset, 0.0f);
        mProjectionSizeOffset = std::max(mProjectionSizeOffset, 0.0f);
        mCameraBackOffset = std::max(mCameraBackOffset, 0.0f);
        mCasterDepthExpansionScale = std::max(mCasterDepthExpansionScale, 0.0f);
        mProjectionCoverageScale = std::max(mProjectionCoverageScale, 1.0f);
        mFirstCascadeCoverageDistance = std::max(mFirstCascadeCoverageDistance, mMinimumViewDistance);
        mSecondCascadeCoverageScale = std::max(mSecondCascadeCoverageScale, 1.0f);
        mParallelDirectionThreshold = std::clamp(mParallelDirectionThreshold, 0.0f, 1.0f);
        mMinimumGridSize = std::max(mMinimumGridSize, 0.0f);
        mMinimumAspectRatio = std::max(mMinimumAspectRatio, 0.0f);
        mMinimumFovDegrees = std::max(mMinimumFovDegrees, 0.0f);
        mMaximumFovDegrees = std::max(mMaximumFovDegrees, mMinimumFovDegrees);
        mClipWMinimum = std::max(mClipWMinimum, 0.0f);
        mMinimumProjectionDivisor = std::max(mMinimumProjectionDivisor, 0.0f);
        mMinimumProjectionDepthSpan = std::max(mMinimumProjectionDepthSpan, 0.0f);
        mDefaultDirectionalLightIntensity = std::max(mDefaultDirectionalLightIntensity, 0.0f);
        mDefaultAmbientLightIntensity = std::max(mDefaultAmbientLightIntensity, 0.0f);
        mCascadeMaximumDistance = std::max(mCascadeMaximumDistance, mMinimumNearPlane + mMinimumViewDistance);
        mCascadeSplitLambda = std::clamp(mCascadeSplitLambda, 0.0f, 1.0f);
        mCascadeNearRangeExpansionDistance = std::max(mCascadeNearRangeExpansionDistance, 0.0f);
        mCascadeExpandedBoundaryCount = std::clamp(mCascadeExpandedBoundaryCount, 0, static_cast<std::int32_t>(mCascadeCount));
    }

    const std::string& ShadowMappingParameterSystem::Name() const {
        return mName;
    }

    Phase ShadowMappingParameterSystem::GetPhase() const {
        return Phase::RenderPrepare;
    }

    std::span<const ComponentAccess> ShadowMappingParameterSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 4> Accesses{ { { typeid(Transform), Access::Read }, { typeid(Camera), Access::Read }, { typeid(DirectionalLight), Access::Read }, { typeid(EntityHierarchy), Access::Read } } };
        return Accesses;
    }

    std::span<const ResourceAccess> ShadowMappingParameterSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 1> Accesses{ { { typeid(RenderContract::RenderFrameData), Access::Write } } };
        return Accesses;
    }

    void ShadowMappingParameterSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        const RenderContract::DirectionalLightParameter DirectionalLightParameter{ BuildDirectionalLightParameter(World) };
        const bool IsShadowMappingEnabled{ Config::Query()->Get<bool>("ShadowMapping_Enabled") };
        RenderContract::FrameRenderWriter FrameWriter{ Ctx.RenderData };

        for (auto [TransformComponent, CameraComponent] : World.Query<Transform, Camera>()) {
            if (CameraComponent.isActive == false) {
                continue;
            }

            RenderContract::ShadowMappingParameter ShadowMappingParameter{};
            if (IsShadowMappingEnabled == true) {
                ShadowMappingParameter = BuildShadowMappingParameter(CameraComponent, TransformComponent, DirectionalLightParameter);
            }
            else {
                ShadowMappingParameter.mDirectionalLight = DirectionalLightParameter;
                ShadowMappingParameter.mDirectionalLight.mFlags &= ~RenderContract::DirectionalLightParameterFlagCastShadow;
                ShadowMappingParameter.mCascadeCount = 0u;
                ShadowMappingParameter.mMinimumShadowMapSize = mMinimumShadowMapSize;
                ShadowMappingParameter.mMinimumProjectionDivisor = mMinimumProjectionDivisor;
                ShadowMappingParameter.mMinimumProjectionDepthSpan = mMinimumProjectionDepthSpan;
            }

            FrameWriter.SetShadowMappingParameter(ShadowMappingParameter);
            break;
        }
    }

    RenderContract::DirectionalLightParameter ShadowMappingParameterSystem::BuildDirectionalLightParameter(Arche::World& World) const {
        bool HasDirectionalLightComponent{ false };
        RenderContract::DirectionalLightParameter FirstDirectionalLightParameter{};

        for (auto [LightComponent, HierarchyComponent] : World.Query<DirectionalLight, EntityHierarchy>()) {
            const Transform* TransformComponent{ World.GetComponent<Transform>(HierarchyComponent.self) };
            if (HasDirectionalLightComponent == false) {
                HasDirectionalLightComponent = true;
                FirstDirectionalLightParameter = BuildDirectionalLightParameterFromComponent(LightComponent, TransformComponent);
            }

            if (LightComponent.mIsActive == false) {
                continue;
            }

            return BuildDirectionalLightParameterFromComponent(LightComponent, TransformComponent);
        }

        if (HasDirectionalLightComponent == true) {
            return FirstDirectionalLightParameter;
        }

        return BuildDefaultDirectionalLightParameter(mDefaultDirectionalLightDirection, mDefaultDirectionalLightColor, mDefaultDirectionalLightIntensity, mDefaultAmbientLightIntensity, mDefaultDirectionalLightCastsShadow);
    }

    RenderContract::ShadowMappingParameter ShadowMappingParameterSystem::BuildShadowMappingParameter(const Camera& CameraComponent, const Transform& TransformComponent, const RenderContract::DirectionalLightParameter& DirectionalLightParameter) const {
        RenderContract::ShadowMappingParameter Parameter{};
        const DirectX::SimpleMath::Vector3 NormalizedLightDirection{ NormalizeDirection(DirectX::SimpleMath::Vector3{ DirectionalLightParameter.mDirection.x, DirectionalLightParameter.mDirection.y, DirectionalLightParameter.mDirection.z }) };
        const int CascadeCount{ static_cast<int>(std::max<std::uint32_t>(1u, std::min(mCascadeCount, RenderContract::ShadowCascadeMaxCount))) };
        Parameter.mDirectionalLight = DirectionalLightParameter;
        Parameter.mCascadeCount = static_cast<std::uint32_t>(CascadeCount);
        Parameter.mMinimumShadowMapSize = mMinimumShadowMapSize;
        Parameter.mMinimumProjectionDivisor = mMinimumProjectionDivisor;
        Parameter.mMinimumProjectionDepthSpan = mMinimumProjectionDepthSpan;

        for (int CascadeIndex{ 0 }; CascadeIndex < CascadeCount; CascadeIndex += 1) {
            const std::size_t ShadowCascadeIndex{ static_cast<std::size_t>(CascadeIndex) };
            Parameter.mShadowMapSizes[ShadowCascadeIndex] = mShadowMapSizes[ShadowCascadeIndex];
            Parameter.mShadowBiases[ShadowCascadeIndex] = mShadowBiases[ShadowCascadeIndex];
            Parameter.mShadowStrengths[ShadowCascadeIndex] = mShadowStrengths[ShadowCascadeIndex];
            Parameter.mRasterDepthBiases[ShadowCascadeIndex] = mRasterDepthBiases[ShadowCascadeIndex];
            Parameter.mRasterSlopeScaledDepthBiases[ShadowCascadeIndex] = mRasterSlopeScaledDepthBiases[ShadowCascadeIndex];

            const ShadowCascadeRange CascadeRange{ ComputeCascadeRange(CameraComponent, CascadeIndex, CascadeCount, mCascadeSplitLambda, mCascadeMaximumDistance, mCascadeNearRangeExpansionDistance, mCascadeExpandedBoundaryCount, mMinimumNearPlane, mMinimumViewDistance, mFirstCascadeCoverageDistance, mSecondCascadeCoverageScale) };
            const std::array<DirectX::SimpleMath::Vector3, 8> FrustumCorners{ BuildCameraFrustumCorners(CameraComponent, TransformComponent, CascadeRange, mMinimumNearPlane, mMinimumExtent, mMinimumViewDistance, mMinimumAspectRatio, mMinimumFovDegrees, mMaximumFovDegrees, mClipWMinimum) };
            DirectX::SimpleMath::Matrix ShadowView{};
            DirectX::SimpleMath::Matrix ShadowProjection{};
            DirectX::SimpleMath::Matrix ShadowViewProjection{};
            DirectX::SimpleMath::Vector3 ShadowCameraPosition{};
            float ShadowNearPlane{ 0.0f };
            float ShadowFarPlane{ 0.0f };
            float ShadowAspectRatio{ 1.0f };
            BuildShadowCameraFromFrustumCorners(FrustumCorners, NormalizedLightDirection, Parameter.mShadowMapSizes[ShadowCascadeIndex], mProjectionCoverageScale, mMinimumExtent, mMinimumNearPlane, mMinimumFarOffset, mNearPlaneOffset, mFarPlaneOffset, mProjectionSizeOffset, mCameraBackOffset, mCasterDepthExpansionScale, mParallelDirectionThreshold, mMinimumGridSize, mMinimumShadowMapSize, mViewProjectionStabilizationEnabled, ShadowView, ShadowProjection, ShadowViewProjection, ShadowCameraPosition, ShadowNearPlane, ShadowFarPlane, ShadowAspectRatio);

            Parameter.mShadowCameras[CascadeIndex].mView = ShadowView;
            Parameter.mShadowCameras[CascadeIndex].mProj = ShadowProjection;
            Parameter.mShadowCameras[CascadeIndex].mViewProj = ShadowViewProjection;
            Parameter.mShadowCameras[CascadeIndex].mPosition = DirectX::SimpleMath::Vector4{ ShadowCameraPosition.x, ShadowCameraPosition.y, ShadowCameraPosition.z, 1.0f };
            Parameter.mShadowCameras[CascadeIndex].mNearPlane = ShadowNearPlane;
            Parameter.mShadowCameras[CascadeIndex].mFarPlane = ShadowFarPlane;
            Parameter.mShadowCameras[CascadeIndex].mAspectRatio = ShadowAspectRatio;
            Parameter.mShadowCameras[CascadeIndex].mFovRadians = 0.0f;

            SetCascadeSplitDistance(Parameter.mCascadeSplitDistances, CascadeIndex, CascadeRange.farPlane);
        }

        Parameter.mDirectionalLight.mDirection = DirectX::SimpleMath::Vector4{ NormalizedLightDirection.x, NormalizedLightDirection.y, NormalizedLightDirection.z, 0.0f };

        return Parameter;
    }
}
