#include "CameraInputSystem.h"
#include <array>
#include "Game/Base/Input.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/Intents/CameraIntent.h"

namespace Game {
    const std::string& CameraInputSystem::Name() const {
        return mName;
    }

    Phase CameraInputSystem::GetPhase() const {
        return Phase::PreUpdate;
    }

    std::span<const ComponentAccess> CameraInputSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 2> Accesses{ { { typeid(CameraIntent), Access::Write }, { typeid(Camera), Access::Read } } };
        return Accesses;
    }

    std::span<const ResourceAccess> CameraInputSystem::ResourceAccesses() const {
        return std::span<const ResourceAccess>{};
    }

    void CameraInputSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        for (auto [Intent, Camera] : World.Query<CameraIntent, Camera>()) {
            if (!Camera.isActive) {
                continue;
            }

            Intent.Reset();

            const CameraControlMode Mode{ ResolveMode(Camera) };
            ProcessMode(Intent, Mode);
        }
    }

    CameraInputSystem::CameraControlMode CameraInputSystem::ResolveMode(const Camera& Camera) const {
        if ((Camera.cameraFlags & Camera::Flags::Cinematic) != 0) {
            return CameraControlMode::Cinematic;
        }

        if ((Camera.cameraFlags & Camera::Flags::FreeLook) != 0) {
            return CameraControlMode::FreeLook;
        }

        if ((Camera.cameraFlags & Camera::Flags::ThirdPerson) != 0) {
            return CameraControlMode::ThirdPerson;
        }

        return CameraControlMode::None;
    }

    void CameraInputSystem::ProcessMode(CameraIntent& Intent, CameraControlMode Mode) {
        switch (Mode) {
            case CameraControlMode::Cinematic:
                ProcessCinematicMode(Intent);
                break;

            case CameraControlMode::FreeLook:
                ProcessFreeLookMode(Intent);
                break;

            case CameraControlMode::ThirdPerson:
                ProcessThirdPersonMode(Intent);
                break;

            case CameraControlMode::None:
            default:
                ProcessDefaultMode(Intent);
                break;
        }
    }

    void CameraInputSystem::ProcessCinematicMode(CameraIntent& Intent) {
        (void)Intent;
    }

    void CameraInputSystem::ProcessFreeLookMode(CameraIntent& Intent) {
        const Globals::Input& Input{ Globals::Input::Get() };
        const float MoveSpeedScale{ Input.IsKeyDown(DirectX::Keyboard::Keys::LeftShift) ? 2.0f : 1.0f };
        SimpleMath::Vector3 MoveDirection{};

        if (Input.IsKeyDown(DirectX::Keyboard::Keys::W)) {
            MoveDirection.z += 1.0f;
        }

        if (Input.IsKeyDown(DirectX::Keyboard::Keys::S)) {
            MoveDirection.z -= 1.0f;
        }

        if (Input.IsKeyDown(DirectX::Keyboard::Keys::D)) {
            MoveDirection.x += 1.0f;
        }

        if (Input.IsKeyDown(DirectX::Keyboard::Keys::A)) {
            MoveDirection.x -= 1.0f;
        }

        if (Input.IsKeyDown(DirectX::Keyboard::Keys::E)) {
            MoveDirection.y += 1.0f;
        }

        if (Input.IsKeyDown(DirectX::Keyboard::Keys::Q)) {
            MoveDirection.y -= 1.0f;
        }

        if (MoveDirection.LengthSquared() > 0.0f) {
            MoveDirection.Normalize();
        }

        Intent.moveDirection = MoveDirection * MoveSpeedScale;
        Intent.lookDelta = SimpleMath::Vector2{ Input.GetMouseDeltaX(), Input.GetMouseDeltaY() };

        const auto& MouseState{ Input.GetMouseState() };
        int LastWheelValue{ MouseState.scrollWheelValue };

        const int CurrentWheelValue{ MouseState.scrollWheelValue };
        const int WheelDelta{ CurrentWheelValue - LastWheelValue };
        LastWheelValue = CurrentWheelValue;

        Intent.zoomDelta = static_cast<float>(WheelDelta) / 120.0f;
    }

    void CameraInputSystem::ProcessThirdPersonMode(CameraIntent& Intent) {
        (void)Intent;
    }

    void CameraInputSystem::ProcessDefaultMode(CameraIntent& Intent) {
        (void)Intent;
    }
}
