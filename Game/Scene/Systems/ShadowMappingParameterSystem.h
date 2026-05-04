#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include "Game/Scene/System.h"

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
        std::array<float, RFD::ShadowCascadeMaxCount> mShadowMapSizes{ 4096.0f, 2048.0f, 2048.0f, 2048.0f };
        std::array<float, RFD::ShadowCascadeMaxCount> mShadowBiases{ 0.0010f, 0.0010f, 0.0010f, 0.0010f };
        std::array<float, RFD::ShadowCascadeMaxCount> mShadowStrengths{ 0.85f, 0.85f, 0.85f, 0.85f };
        std::array<float, RFD::ShadowCascadeMaxCount> mRasterDepthBiases{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::array<float, RFD::ShadowCascadeMaxCount> mRasterSlopeScaledDepthBiases{ 1.25f, 1.25f, 1.25f, 1.25f };
        float mCascadeMaximumDistance{ 200.0f };
        float mCascadeSplitLambda{ 0.90f };
        float mCascadeNearRangeExpansionDistance{ 9.4f };
        std::int32_t mCascadeExpandedBoundaryCount{ 3 };
    };
}
