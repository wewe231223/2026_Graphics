#include "CameraInputSystem.h"
#include <array>
#include "Imgui/imgui.h"
#include "Core/Config.h"
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
        static std::array<ResourceAccess, 1> Accesses{ { { typeid(Arche::EntityID), Access::Read } } };
        return Accesses;
    }

    void CameraInputSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        const Globals::Input& Input{ Globals::Input::Get() };
        const DirectX::Mouse::ButtonStateTracker& MouseTracker{ Input.GetMouseTracker() };
        const bool IsSelectionDragInput{ Ctx.PickedEntityId != Arche::NullEntityID && (MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::PRESSED || MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::HELD) };
        bool IsUiCapturingInput{ false };
        if (!Config::Query()->Get<bool>("Block_ImGui")) {
            const ImGuiIO& ImGuiInputState{ ImGui::GetIO() };
            const bool IsUiCapturingMouseInput{ ImGuiInputState.WantCaptureMouse };
            const bool IsUiCapturingKeyboardInput{ ImGuiInputState.WantCaptureKeyboard };
            IsUiCapturingInput = IsUiCapturingMouseInput || IsUiCapturingKeyboardInput;
        }

        for (auto [Intent, Camera] : World.Query<CameraIntent, Camera>()) {
            if (!Camera.isActive) {
                continue;
            }

            Intent.Reset();
            if (IsSelectionDragInput || IsUiCapturingInput) {
                continue;
            }

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
            MoveDirection += DirectX::SimpleMath::Vector3::Forward * Dt;
        }

        if (Input.IsKeyDown(DirectX::Keyboard::Keys::S)) {
            MoveDirection += DirectX::SimpleMath::Vector3::Backward * Dt;
        }

        if (Input.IsKeyDown(DirectX::Keyboard::Keys::D)) {
            MoveDirection += DirectX::SimpleMath::Vector3::Left * Dt;
        }

        if (Input.IsKeyDown(DirectX::Keyboard::Keys::A)) {
            MoveDirection += DirectX::SimpleMath::Vector3::Right * Dt;
        }

        if (Input.IsKeyDown(DirectX::Keyboard::Keys::E)) {
            MoveDirection += DirectX::SimpleMath::Vector3::Up * Dt;
        }

        if (Input.IsKeyDown(DirectX::Keyboard::Keys::Q)) {
            MoveDirection += DirectX::SimpleMath::Vector3::Down * Dt;
        }

        if (MoveDirection.LengthSquared() > 0.0f) {
            MoveDirection.Normalize();
        }

        Intent.moveDirection = MoveDirection * MoveSpeedScale;

        const DirectX::Mouse::ButtonStateTracker& MouseTracker{ Input.GetMouseTracker() };
        const bool IsPickingInteraction{ MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::PRESSED || MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::HELD };
        if (IsPickingInteraction == false) {
            Intent.lookDelta = SimpleMath::Vector2{ static_cast<float>(Input.GetMouseDeltaX()) * Dt, static_cast<float>(Input.GetMouseDeltaY()) * Dt };
        }

        const auto& MouseState{ Input.GetMouseState() };
        static int LastWheelValue{ MouseState.scrollWheelValue };

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
