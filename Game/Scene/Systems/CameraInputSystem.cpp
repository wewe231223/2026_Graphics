#include "CameraInputSystem.h"
#include <array>
#include <format>
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

            const CameraControlMode Mode{ ResolveMode(Camera, Dt) };
            ProcessMode(Intent, Mode, Dt);
        }
    }

    CameraInputSystem::CameraControlMode CameraInputSystem::ResolveMode(const Camera& Camera, float Dt) const {
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

    void CameraInputSystem::ProcessMode(CameraIntent& Intent, CameraControlMode Mode, float Dt) {
        switch (Mode) {
            case CameraControlMode::Cinematic:
                ProcessCinematicMode(Intent, Dt);
                break;

            case CameraControlMode::FreeLook:
                ProcessFreeLookMode(Intent, Dt);
                break;

            case CameraControlMode::ThirdPerson:
                ProcessThirdPersonMode(Intent, Dt);
                break;

            case CameraControlMode::None:
            default:
                ProcessDefaultMode(Intent, Dt);
                break;
        }
    }

    void CameraInputSystem::ProcessCinematicMode(CameraIntent& Intent, float Dt) {
        (void)Intent;
    }

    void CameraInputSystem::ProcessFreeLookMode(CameraIntent& Intent, float Dt) {
        const Globals::Input& Input{ Globals::Input::Get() };
        const float MoveSpeedScale{ Input.IsKeyDown(DirectX::Keyboard::Keys::LeftShift) ? 2.0f : 1.0f };
        SimpleMath::Vector3 MoveDirection{};

        if (Input.IsKeyDown(DirectX::Keyboard::Keys::W)) {
            MoveDirection += DirectX::SimpleMath::Vector3::UnitZ * Dt;
        }

        if (Input.IsKeyDown(DirectX::Keyboard::Keys::S)) {
            MoveDirection -= DirectX::SimpleMath::Vector3::UnitZ * Dt;
        }

        if (Input.IsKeyDown(DirectX::Keyboard::Keys::D)) {
            MoveDirection += DirectX::SimpleMath::Vector3::UnitX * Dt;
        }

        if (Input.IsKeyDown(DirectX::Keyboard::Keys::A)) {
            MoveDirection -= DirectX::SimpleMath::Vector3::UnitX * Dt;
        }

        if (Input.IsKeyDown(DirectX::Keyboard::Keys::E)) {
            MoveDirection += DirectX::SimpleMath::Vector3::UnitY * Dt;
        }

        if (Input.IsKeyDown(DirectX::Keyboard::Keys::Q)) {
            MoveDirection -= DirectX::SimpleMath::Vector3::UnitY * Dt;
        }

        if (MoveDirection.LengthSquared() > 0.0f) {
            MoveDirection.Normalize();
        }

        Intent.moveDirection = MoveDirection * MoveSpeedScale;
        Intent.lookDelta = SimpleMath::Vector2{ Input.GetMouseDeltaX() * Dt, Input.GetMouseDeltaY() * Dt};

        //OutputDebugStringA(std::format("Mouse Delta: ({:.2f}, {:.2f})\n", Input.GetMouseDeltaX(), Input.GetMouseDeltaY()).c_str());

        const auto& MouseState{ Input.GetMouseState() };
        int LastWheelValue{ MouseState.scrollWheelValue };

        const int CurrentWheelValue{ MouseState.scrollWheelValue };
        const int WheelDelta{ CurrentWheelValue - LastWheelValue };
        LastWheelValue = CurrentWheelValue;

        Intent.zoomDelta = static_cast<float>(WheelDelta) / 120.0f;
    }

    void CameraInputSystem::ProcessThirdPersonMode(CameraIntent& Intent, float Dt) {
        (void)Intent;
    }

    void CameraInputSystem::ProcessDefaultMode(CameraIntent& Intent, float Dt) {
        (void)Intent;
    }
}
