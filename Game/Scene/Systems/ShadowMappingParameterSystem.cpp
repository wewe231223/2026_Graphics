#include "ShadowMappingParameterSystem.h"
#include <array>
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    constexpr DirectX::SimpleMath::Vector3 ShadowLightDirection{ 0.4f, -1.0f, 0.35f };
    constexpr float ShadowFocusDistance{ 20.0f };
    constexpr float ShadowOrthographicWidth{ 60.0f };
    constexpr float ShadowOrthographicHeight{ 60.0f };
    constexpr float ShadowNearPlane{ 0.1f };
    constexpr float ShadowFarPlane{ 160.0f };
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
        (void)CameraComponent;

        RFD::ShadowMappingParameter Parameter{};
        DirectX::SimpleMath::Vector3 CameraForwardDirection{ DirectX::SimpleMath::Vector3::Transform(DirectX::SimpleMath::Vector3::Forward, TransformComponent.rotation) };
        CameraForwardDirection.Normalize();
        const DirectX::SimpleMath::Vector3 FocusPosition{ TransformComponent.position + (CameraForwardDirection * ShadowFocusDistance) };
        DirectX::SimpleMath::Vector3 NormalizedLightDirection{ ShadowLightDirection };
        NormalizedLightDirection.Normalize();
        const DirectX::SimpleMath::Vector3 ShadowCameraPosition{ FocusPosition - (NormalizedLightDirection * ShadowFocusDistance) };
        const DirectX::SimpleMath::Matrix ShadowView{ DirectX::SimpleMath::Matrix::CreateLookAt(ShadowCameraPosition, FocusPosition, DirectX::SimpleMath::Vector3::Up) };
        const DirectX::SimpleMath::Matrix ShadowProj{ DirectX::SimpleMath::Matrix::CreateOrthographic(ShadowOrthographicWidth, ShadowOrthographicHeight, ShadowNearPlane, ShadowFarPlane) };

        Parameter.shadowCamera.view = ShadowView;
        Parameter.shadowCamera.proj = ShadowProj;
        Parameter.shadowCamera.viewProj = ShadowView * ShadowProj;
        Parameter.shadowCamera.position = DirectX::SimpleMath::Vector4{ ShadowCameraPosition.x, ShadowCameraPosition.y, ShadowCameraPosition.z, 1.0f };
        Parameter.shadowCamera.nearPlane = ShadowNearPlane;
        Parameter.shadowCamera.farPlane = ShadowFarPlane;
        Parameter.shadowCamera.aspectRatio = ShadowOrthographicWidth / ShadowOrthographicHeight;
        Parameter.shadowCamera.fovRadians = 0.0f;
        Parameter.lightDirection = DirectX::SimpleMath::Vector4{ NormalizedLightDirection.x, NormalizedLightDirection.y, NormalizedLightDirection.z, 0.0f };

        return Parameter;
    }
}
