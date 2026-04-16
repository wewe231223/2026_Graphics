#pragma once
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
        RFD::ShadowMappingParameter BuildShadowMappingParameter(const Camera& CameraComponent, const Transform& TransformComponent) const;

    private:
        const std::string mName{ "ShadowMappingParameterSystem" };
        const std::filesystem::path mShadowMappingParameterFilePath{ "Resources/ShadowMappingParameter.yaml" };
        float mShadowMapSize{ 2048.0f };
        float mShadowBias{ 0.0010f };
        float mShadowStrength{ 0.6f };
        float mRasterDepthBias{ 1.0f };
        float mRasterSlopeScaledDepthBias{ 1.25f };
        float mCascadeMaximumDistance{ 200.0f };
        float mCascadeSplitLambda{ 0.90f };
        float mCascadeNearRangeExpansionDistance{ 9.4f };
        std::int32_t mCascadeExpandedBoundaryCount{ 3 };
    };
}
