#pragma once
#include <string>
#include "Game/Scene/System.h"

namespace Game {
    struct Camera;
    struct Transform;

    class ShadowMappingParameterSystem final : public ISystem {
    public:
        ShadowMappingParameterSystem() = default;
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
        RFD::ShadowMappingParameter BuildShadowMappingParameter(const Camera& CameraComponent, const Transform& TransformComponent) const;

    private:
        const std::string mName{ "ShadowMappingParameterSystem" };
    };
}
