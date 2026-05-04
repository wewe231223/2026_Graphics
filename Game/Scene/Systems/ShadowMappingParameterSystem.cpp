#include "ShadowMappingParameterSystem.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/DirectionalLight.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Transform.h"


#undef min
#undef max 

#include <ryml.hpp>
#include <ryml_std.hpp>

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
    constexpr float ShadowCasterDepthExpansionScale{ 1.0f };
    constexpr float ShadowCascadeProjectionCoverageScale{ 1.10f };
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

    Game::RFD::DirectionalLightParameter BuildDefaultDirectionalLightParameter() {
        const DirectX::SimpleMath::Vector3 NormalizedLightDirection{ NormalizeDirection(ShadowLightDirection) };
        Game::RFD::DirectionalLightParameter Parameter{};
        Parameter.direction = DirectX::SimpleMath::Vector4{ NormalizedLightDirection.x, NormalizedLightDirection.y, NormalizedLightDirection.z, 0.0f };
        Parameter.color = DirectX::SimpleMath::Vector4{ 1.0f, 0.97f, 0.92f, 1.0f };
        Parameter.intensity = 1.2f;
        Parameter.ambientIntensity = 0.25f;
        Parameter.flags = Game::RFD::DirectionalLightParameterFlagActive | Game::RFD::DirectionalLightParameterFlagCastShadow;
        Parameter.padding0 = 0.0f;
        return Parameter;
    }

    DirectX::SimpleMath::Vector3 ResolveDirectionalLightDirection(const Game::DirectionalLight& LightComponent, const Game::Transform* TransformComponent) {
        if (LightComponent.mUseTransformDirection == true && TransformComponent != nullptr) {
            return TransformComponent->GetForwardDirection();
        }

        return LightComponent.mDirection;
    }

    Game::RFD::DirectionalLightParameter BuildDirectionalLightParameterFromComponent(const Game::DirectionalLight& LightComponent, const Game::Transform* TransformComponent) {
        const DirectX::SimpleMath::Vector3 NormalizedLightDirection{ NormalizeDirection(ResolveDirectionalLightDirection(LightComponent, TransformComponent)) };
        Game::RFD::DirectionalLightParameter Parameter{};
        Parameter.direction = DirectX::SimpleMath::Vector4{ NormalizedLightDirection.x, NormalizedLightDirection.y, NormalizedLightDirection.z, 0.0f };
        Parameter.color = DirectX::SimpleMath::Vector4{ LightComponent.mColor.x, LightComponent.mColor.y, LightComponent.mColor.z, 1.0f };
        Parameter.intensity = std::max(LightComponent.mIntensity, 0.0f);
        Parameter.ambientIntensity = std::max(LightComponent.mAmbientIntensity, 0.0f);
        Parameter.flags = 0u;
        if (LightComponent.mIsActive == true) {
            Parameter.flags |= Game::RFD::DirectionalLightParameterFlagActive;
        }

        if (LightComponent.mIsActive == true && LightComponent.mCastsShadow == true) {
            Parameter.flags |= Game::RFD::DirectionalLightParameterFlagCastShadow;
        }

        Parameter.padding0 = 0.0f;
        return Parameter;
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

    ShadowCascadeRange ComputeCascadeRange(const Game::Camera& CameraComponent, int CascadeIndex, int CascadeCount, float CascadeSplitLambda, float CascadeMaximumDistance, float CascadeNearRangeExpansionDistance, int CascadeExpandedBoundaryCount) {
        const float EffectiveNearPlane{ std::max(CameraComponent.nearPlane, ShadowMinimumNearPlane) };
        const float MinimumFarPlane{ EffectiveNearPlane + ShadowMinimumViewDistance };
        const float MaximumFarPlane{ std::max(CascadeMaximumDistance, MinimumFarPlane) };
        const float EffectiveFarPlane{ std::clamp(CameraComponent.farPlane, MinimumFarPlane, MaximumFarPlane) };
        const int EffectiveCascadeIndex{ std::clamp(CascadeIndex, 0, std::max(CascadeCount - 1, 0)) };
        const float CascadeNearPlaneBase{ ComputeCascadeSplitDistance(EffectiveNearPlane, EffectiveFarPlane, EffectiveCascadeIndex, CascadeCount, CascadeSplitLambda) };
        const float CascadeFarPlaneBase{ ComputeCascadeSplitDistance(EffectiveNearPlane, EffectiveFarPlane, EffectiveCascadeIndex + 1, CascadeCount, CascadeSplitLambda) };
        const int CascadeNearBoundaryExpansionStepCount{ std::clamp(EffectiveCascadeIndex, 0, CascadeExpandedBoundaryCount) };
        const int CascadeFarBoundaryExpansionStepCount{ std::clamp(EffectiveCascadeIndex + 1, 0, CascadeExpandedBoundaryCount) };
        const float CascadeNearPlane{ std::clamp(CascadeNearPlaneBase + (static_cast<float>(CascadeNearBoundaryExpansionStepCount) * CascadeNearRangeExpansionDistance), EffectiveNearPlane, EffectiveFarPlane) };
        const float CascadeFarPlane{ std::clamp(CascadeFarPlaneBase + (static_cast<float>(CascadeFarBoundaryExpansionStepCount) * CascadeNearRangeExpansionDistance), CascadeNearPlane, EffectiveFarPlane) };
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

        const float ShadowCasterDepthExpansionDistance{ std::max(FrustumRadius * ShadowCasterDepthExpansionScale, ShadowCameraBackOffset) };
        const float ShadowLightDistance{ std::max(FrustumRadius + ShadowCasterDepthExpansionDistance, ShadowMinimumNearPlane + ShadowFarPlaneOffset + ShadowMinimumFarOffset) };
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
        OutShadowNearPlane = std::max(ShadowMinimumNearPlane, MinimumDepth - ShadowCasterDepthExpansionDistance - ShadowNearPlaneOffset);
        OutShadowFarPlane = std::max(OutShadowNearPlane + ShadowMinimumFarOffset, MaximumDepth + ShadowFarPlaneOffset);
        OutShadowAspectRatio = ShadowProjectionWidth / std::max(ShadowProjectionHeight, ShadowMinimumExtent);
        OutShadowProjection = DirectX::SimpleMath::Matrix::CreateOrthographicOffCenter(ProjectionLeft, ProjectionRight, ProjectionBottom, ProjectionTop, OutShadowNearPlane, OutShadowFarPlane);
        OutShadowViewProjection = OutShadowView * OutShadowProjection;
        if (ShadowViewProjectionStabilizationEnabled == true) {
            StabilizeShadowViewProjection(OutShadowViewProjection, ShadowMapSize);
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
                bool IsCascadeParameterLoaded{ false };
                if (ShadowMappingNode.has_child("Cascades")) {
                    const c4::yml::ConstNodeRef CascadeNodes{ ShadowMappingNode["Cascades"] };
                    if (CascadeNodes.invalid() == false && CascadeNodes.is_seq() == true) {
                        std::size_t CascadeIndex{ 0 };
                        for (const c4::yml::ConstNodeRef CascadeNode : CascadeNodes.children()) {
                            if (CascadeIndex >= RFD::ShadowCascadeMaxCount) {
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

                    for (std::size_t CascadeIndex{ 0 }; CascadeIndex < RFD::ShadowCascadeMaxCount; CascadeIndex += 1) {
                        mShadowMapSizes[CascadeIndex] = LegacyShadowMapSize;
                        mShadowBiases[CascadeIndex] = LegacyShadowBias;
                        mShadowStrengths[CascadeIndex] = LegacyShadowStrength;
                        mRasterDepthBiases[CascadeIndex] = LegacyRasterDepthBias;
                        mRasterSlopeScaledDepthBiases[CascadeIndex] = LegacyRasterSlopeScaledDepthBias;
                    }

                    if (RFD::ShadowCascadeMaxCount > 0u) {
                        mShadowMapSizes[0] = LegacyShadowMapSize * 2.0f;
                    }
                }

                if (ShadowMappingNode.has_child("CascadeMaximumDistance")) {
                    ShadowMappingNode["CascadeMaximumDistance"] >> mCascadeMaximumDistance;
                }

                if (ShadowMappingNode.has_child("CascadeSplitLambda")) {
                    ShadowMappingNode["CascadeSplitLambda"] >> mCascadeSplitLambda;
                }

                if (ShadowMappingNode.has_child("CascadeNearRangeExpansionDistance")) {
                    ShadowMappingNode["CascadeNearRangeExpansionDistance"] >> mCascadeNearRangeExpansionDistance;
                }

                if (ShadowMappingNode.has_child("CascadeExpandedBoundaryCount")) {
                    std::int32_t CascadeExpandedBoundaryCountValue{ mCascadeExpandedBoundaryCount };
                    ShadowMappingNode["CascadeExpandedBoundaryCount"] >> CascadeExpandedBoundaryCountValue;
                    mCascadeExpandedBoundaryCount = CascadeExpandedBoundaryCountValue;
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
        OutputStream << "  Cascades:\n";
        for (std::size_t CascadeIndex{ 0 }; CascadeIndex < RFD::ShadowCascadeMaxCount; CascadeIndex += 1) {
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
    }

    void ShadowMappingParameterSystem::SanitizeShadowMappingParameters() {
        for (std::size_t CascadeIndex{ 0 }; CascadeIndex < RFD::ShadowCascadeMaxCount; CascadeIndex += 1) {
            mShadowMapSizes[CascadeIndex] = std::max(mShadowMapSizes[CascadeIndex], 1.0f);
            mShadowBiases[CascadeIndex] = std::max(mShadowBiases[CascadeIndex], 0.0f);
            mShadowStrengths[CascadeIndex] = std::clamp(mShadowStrengths[CascadeIndex], 0.0f, 1.0f);
            mRasterDepthBiases[CascadeIndex] = std::max(mRasterDepthBiases[CascadeIndex], 0.0f);
            mRasterSlopeScaledDepthBiases[CascadeIndex] = std::max(mRasterSlopeScaledDepthBiases[CascadeIndex], 0.0f);
        }

        mCascadeMaximumDistance = std::max(mCascadeMaximumDistance, ShadowMinimumNearPlane + ShadowMinimumViewDistance);
        mCascadeSplitLambda = std::clamp(mCascadeSplitLambda, 0.0f, 1.0f);
        mCascadeNearRangeExpansionDistance = std::max(mCascadeNearRangeExpansionDistance, 0.0f);
        mCascadeExpandedBoundaryCount = std::clamp(mCascadeExpandedBoundaryCount, 0, static_cast<std::int32_t>(RFD::ShadowCascadeMaxCount));
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
        static std::array<ResourceAccess, 1> Accesses{ { { typeid(RFD::RenderFrameData), Access::Write } } };
        return Accesses;
    }

    void ShadowMappingParameterSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        const RFD::DirectionalLightParameter DirectionalLightParameter{ BuildDirectionalLightParameter(World) };

        for (auto [TransformComponent, CameraComponent] : World.Query<Transform, Camera>()) {
            if (CameraComponent.isActive == false) {
                continue;
            }

            const RFD::ShadowMappingParameter ShadowMappingParameter{ BuildShadowMappingParameter(CameraComponent, TransformComponent, DirectionalLightParameter) };
            Ctx.RenderData.shadowMapping = ShadowMappingParameter;
            break;
        }
    }

    RFD::DirectionalLightParameter ShadowMappingParameterSystem::BuildDirectionalLightParameter(Arche::World& World) const {
        bool HasDirectionalLightComponent{ false };
        RFD::DirectionalLightParameter FirstDirectionalLightParameter{};

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

        return BuildDefaultDirectionalLightParameter();
    }

    RFD::ShadowMappingParameter ShadowMappingParameterSystem::BuildShadowMappingParameter(const Camera& CameraComponent, const Transform& TransformComponent, const RFD::DirectionalLightParameter& DirectionalLightParameter) const {
        RFD::ShadowMappingParameter Parameter{};
        const DirectX::SimpleMath::Vector3 NormalizedLightDirection{ NormalizeDirection(DirectX::SimpleMath::Vector3{ DirectionalLightParameter.direction.x, DirectionalLightParameter.direction.y, DirectionalLightParameter.direction.z }) };
        const int CascadeCount{ static_cast<int>(std::max<std::uint32_t>(1u, RFD::ShadowCascadeMaxCount)) };
        Parameter.directionalLight = DirectionalLightParameter;
        Parameter.cascadeCount = static_cast<std::uint32_t>(CascadeCount);

        for (int CascadeIndex{ 0 }; CascadeIndex < CascadeCount; CascadeIndex += 1) {
            const std::size_t ShadowCascadeIndex{ static_cast<std::size_t>(CascadeIndex) };
            Parameter.shadowMapSizes[ShadowCascadeIndex] = mShadowMapSizes[ShadowCascadeIndex];
            Parameter.shadowBiases[ShadowCascadeIndex] = mShadowBiases[ShadowCascadeIndex];
            Parameter.shadowStrengths[ShadowCascadeIndex] = mShadowStrengths[ShadowCascadeIndex];
            Parameter.rasterDepthBiases[ShadowCascadeIndex] = mRasterDepthBiases[ShadowCascadeIndex];
            Parameter.rasterSlopeScaledDepthBiases[ShadowCascadeIndex] = mRasterSlopeScaledDepthBiases[ShadowCascadeIndex];

            const ShadowCascadeRange CascadeRange{ ComputeCascadeRange(CameraComponent, CascadeIndex, CascadeCount, mCascadeSplitLambda, mCascadeMaximumDistance, mCascadeNearRangeExpansionDistance, mCascadeExpandedBoundaryCount) };
            const std::array<DirectX::SimpleMath::Vector3, 8> FrustumCorners{ BuildCameraFrustumCorners(CameraComponent, TransformComponent, CascadeRange) };
            DirectX::SimpleMath::Matrix ShadowView{};
            DirectX::SimpleMath::Matrix ShadowProjection{};
            DirectX::SimpleMath::Matrix ShadowViewProjection{};
            DirectX::SimpleMath::Vector3 ShadowCameraPosition{};
            float ShadowNearPlane{ 0.0f };
            float ShadowFarPlane{ 0.0f };
            float ShadowAspectRatio{ 1.0f };
            BuildShadowCameraFromFrustumCorners(FrustumCorners, NormalizedLightDirection, Parameter.shadowMapSizes[ShadowCascadeIndex], ShadowCascadeProjectionCoverageScale, ShadowView, ShadowProjection, ShadowViewProjection, ShadowCameraPosition, ShadowNearPlane, ShadowFarPlane, ShadowAspectRatio);

            Parameter.shadowCameras[CascadeIndex].view = ShadowView;
            Parameter.shadowCameras[CascadeIndex].proj = ShadowProjection;
            Parameter.shadowCameras[CascadeIndex].viewProj = ShadowViewProjection;
            Parameter.shadowCameras[CascadeIndex].position = DirectX::SimpleMath::Vector4{ ShadowCameraPosition.x, ShadowCameraPosition.y, ShadowCameraPosition.z, 1.0f };
            Parameter.shadowCameras[CascadeIndex].nearPlane = ShadowNearPlane;
            Parameter.shadowCameras[CascadeIndex].farPlane = ShadowFarPlane;
            Parameter.shadowCameras[CascadeIndex].aspectRatio = ShadowAspectRatio;
            Parameter.shadowCameras[CascadeIndex].fovRadians = 0.0f;

            SetCascadeSplitDistance(Parameter.cascadeSplitDistances, CascadeIndex, CascadeRange.farPlane);
        }

        Parameter.directionalLight.direction = DirectX::SimpleMath::Vector4{ NormalizedLightDirection.x, NormalizedLightDirection.y, NormalizedLightDirection.z, 0.0f };

        return Parameter;
    }
}
