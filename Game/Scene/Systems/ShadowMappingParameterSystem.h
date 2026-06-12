#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include "Game/Scene/Base/SynchronousSystem.h"

namespace Game {
    struct Camera;
    struct Transform;

    class ShadowMappingParameterSystem final : public ISystem {
    public:
        ShadowMappingParameterSystem();
        ~ShadowMappingParameterSystem() override = default;

        ShadowMappingParameterSystem(const ShadowMappingParameterSystem&) = default;
        ShadowMappingParameterSystem& operator=(const ShadowMappingParameterSystem&) = default;

        ShadowMappingParameterSystem(ShadowMappingParameterSystem&&) noexcept = default;
        ShadowMappingParameterSystem& operator=(ShadowMappingParameterSystem&&) noexcept = default;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        void LoadShadowMappingParameterFile();
        void SaveShadowMappingParameterFile() const;
        void SanitizeShadowMappingParameters();
        RFD::DirectionalLightParameter BuildDirectionalLightParameter(Arche::World& World) const;
        RFD::ShadowMappingParameter BuildShadowMappingParameter(const Camera& CameraComponent, const Transform& TransformComponent, const RFD::DirectionalLightParameter& DirectionalLightParameter) const;

    private:
        const std::string mName{ "ShadowMappingParameterSystem" };
        const std::filesystem::path mShadowMappingParameterFilePath{ "Resources/ShadowMappingParameter.yaml" };
        std::array<float, RFD::ShadowCascadeMaxCount> mShadowMapSizes{};
        std::array<float, RFD::ShadowCascadeMaxCount> mShadowBiases{};
        std::array<float, RFD::ShadowCascadeMaxCount> mShadowStrengths{};
        std::array<float, RFD::ShadowCascadeMaxCount> mRasterDepthBiases{};
        std::array<float, RFD::ShadowCascadeMaxCount> mRasterSlopeScaledDepthBiases{};
        DirectX::SimpleMath::Vector3 mDefaultDirectionalLightDirection{};
        DirectX::SimpleMath::Vector4 mDefaultDirectionalLightColor{};
        std::uint32_t mCascadeCount{};
        std::int32_t mCascadeExpandedBoundaryCount{};
        float mDefaultDirectionalLightIntensity{};
        float mDefaultAmbientLightIntensity{};
        float mCascadeMaximumDistance{};
        float mCascadeSplitLambda{};
        float mCascadeNearRangeExpansionDistance{};
        float mFirstCascadeCoverageDistance{};
        float mSecondCascadeCoverageScale{};
        float mMinimumNearPlane{};
        float mMinimumExtent{};
        float mMinimumViewDistance{};
        float mMinimumFarOffset{};
        float mNearPlaneOffset{};
        float mFarPlaneOffset{};
        float mProjectionSizeOffset{};
        float mCameraBackOffset{};
        float mCasterDepthExpansionScale{};
        float mProjectionCoverageScale{};
        float mParallelDirectionThreshold{};
        float mMinimumGridSize{};
        float mMinimumAspectRatio{};
        float mMinimumFovDegrees{};
        float mMaximumFovDegrees{};
        float mClipWMinimum{};
        float mMinimumShadowMapSize{};
        float mMinimumProjectionDivisor{};
        float mMinimumProjectionDepthSpan{};
        bool mDefaultDirectionalLightCastsShadow{};
        bool mViewProjectionStabilizationEnabled{};
    };
}
