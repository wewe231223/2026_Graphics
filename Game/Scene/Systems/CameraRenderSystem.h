#pragma once
#include <string>
#include "Game/Scene/System.h"

namespace Game {
    struct Camera;
    struct Frustum;
    struct Transform;

    class CameraRenderSystem final : public ISystem {
    public:
        CameraRenderSystem() = default;
        ~CameraRenderSystem() override = default;

        CameraRenderSystem(const CameraRenderSystem&) = default;
        CameraRenderSystem& operator=(const CameraRenderSystem&) = default;

        CameraRenderSystem(CameraRenderSystem&&) noexcept = default;
        CameraRenderSystem& operator=(CameraRenderSystem&&) noexcept = default;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        void ApplyThirdPersonCameraTransform(Arche::World& World, Transform& TransformComponent, Camera& CameraComponent, float Dt) const;
        void UpdateCameraMatricesAndFrustum(const Transform& TransformComponent, Camera& CameraComponent, Frustum& FrustumComponent) const;
        void WriteCameraParameter(const Camera& CameraComponent, const Transform& TransformComponent, RFD::RenderFrameData& RenderData, float Dt) const;

    private:
        const std::string mName{ "CameraRenderSystem" };
    };
}
