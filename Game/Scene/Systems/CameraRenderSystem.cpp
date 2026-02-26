#include "CameraRenderSystem.h"
#include <algorithm>
#include <array>
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/Intents/CameraIntent.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    constexpr float LookSensitivity{ 0.0025f };
    constexpr float MoveSpeedUnitsPerSecond{ 6.0f };
    constexpr float ZoomSpeedDegreesPerTick{ 2.0f };
    constexpr float MinPitchRadians{ -1.55334306f };
    constexpr float MaxPitchRadians{ 1.55334306f };
    constexpr float MinFovDegrees{ 20.0f };
    constexpr float MaxFovDegrees{ 120.0f };
}

namespace Game {
    const std::string& CameraRenderSystem::Name() const {
        return mName;
    }

    Phase CameraRenderSystem::GetPhase() const {
        return Phase::Render;
    }

    std::span<const ComponentAccess> CameraRenderSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 3> Accesses{ { { typeid(Transform), Access::Write }, { typeid(Camera), Access::Write }, { typeid(CameraIntent), Access::Read } } };
        return Accesses;
    }

    std::span<const ResourceAccess> CameraRenderSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 1> Accesses{ { { typeid(RFD::RenderFrameData), Access::Write } } };
        return Accesses;
    }

    void CameraRenderSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        for (auto [TransformComponent, CameraComponent, CameraIntentComponent] : World.Query<Transform, Camera, CameraIntent>()) {
            if (!CameraComponent.isActive) {
                continue;
            }

            ApplyIntentToTransform(TransformComponent, CameraComponent, CameraIntentComponent, Dt);
            WriteRenderGlobalsFromCamera(TransformComponent, CameraComponent, Ctx.RenderData, Dt);
            break;
        }
    }

    void CameraRenderSystem::ApplyIntentToTransform(Transform& TransformComponent, Camera& CameraComponent, const CameraIntent& CameraIntentComponent, float Dt) const {
        const float PitchDelta{ CameraIntentComponent.lookDelta.y * LookSensitivity };
        const float YawDelta{ CameraIntentComponent.lookDelta.x * LookSensitivity };

        TransformComponent.rotationEuler.x = std::clamp(TransformComponent.rotationEuler.x + PitchDelta, MinPitchRadians, MaxPitchRadians);
        TransformComponent.rotationEuler.y += YawDelta;

        const DirectX::SimpleMath::Quaternion RotationQuaternion{ DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(TransformComponent.rotationEuler.y, TransformComponent.rotationEuler.x, TransformComponent.rotationEuler.z) };
        TransformComponent.rotation = RotationQuaternion;

        DirectX::SimpleMath::Vector3 LocalMoveDirection{ CameraIntentComponent.moveDirection };
        if (LocalMoveDirection.LengthSquared() > 0.0f) {
            LocalMoveDirection.Normalize();
        }

        const DirectX::SimpleMath::Vector3 WorldMoveDirection{ DirectX::SimpleMath::Vector3::Transform(LocalMoveDirection, RotationQuaternion) };
        TransformComponent.position += WorldMoveDirection * (MoveSpeedUnitsPerSecond * Dt * CameraIntentComponent.moveDirection.Length());

        CameraComponent.fov = std::clamp(CameraComponent.fov - (CameraIntentComponent.zoomDelta * ZoomSpeedDegreesPerTick), MinFovDegrees, MaxFovDegrees);
    }

    void CameraRenderSystem::WriteRenderGlobalsFromCamera(const Transform& TransformComponent, Camera& CameraComponent, RFD::RenderFrameData& RenderData, float Dt) const {
        const DirectX::SimpleMath::Vector3 ForwardDirection{ DirectX::SimpleMath::Vector3::Transform(DirectX::SimpleMath::Vector3::Forward, TransformComponent.rotation) };
        const DirectX::SimpleMath::Vector3 UpDirection{ DirectX::SimpleMath::Vector3::Transform(DirectX::SimpleMath::Vector3::Up, TransformComponent.rotation) };
        const DirectX::SimpleMath::Vector3 EyePosition{ TransformComponent.position };
        const DirectX::SimpleMath::Vector3 FocusPosition{ EyePosition + ForwardDirection };

        CameraComponent.viewMatrix = DirectX::SimpleMath::Matrix::CreateLookAt(EyePosition, FocusPosition, UpDirection);

        if (CameraComponent.isOrthographic) {
            const float Width{ CameraComponent.orthoSize * CameraComponent.aspectRatio * 2.0f };
            const float Height{ CameraComponent.orthoSize * 2.0f };
            CameraComponent.projMatrix = DirectX::SimpleMath::Matrix::CreateOrthographic(Width, Height, CameraComponent.nearPlane, CameraComponent.farPlane);
        }
        else {
            CameraComponent.projMatrix = DirectX::SimpleMath::Matrix::CreatePerspectiveFieldOfView(DirectX::XMConvertToRadians(CameraComponent.fov), CameraComponent.aspectRatio, CameraComponent.nearPlane, CameraComponent.farPlane);
        }

        RenderData.globals.prevViewProj = RenderData.globals.viewProj;
        RenderData.globals.view = CameraComponent.viewMatrix;
        RenderData.globals.proj = CameraComponent.projMatrix;
        RenderData.globals.viewProj = CameraComponent.viewMatrix * CameraComponent.projMatrix;
        RenderData.globals.dt = Dt;
    }
}