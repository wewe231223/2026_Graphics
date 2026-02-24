#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "Game/Scene/System.h"

/*
+----------------------+------------------------------+
| Input                | Action                       |
+----------------------+------------------------------+
| W / A / S / D        | Move Forward / Left / Back   |
| Q / E                | Move Down / Up               |
| Left Shift           | Move Speed x2                |
| Mouse Delta (X, Y)   | Look Yaw / Pitch             |
| Mouse Wheel          | Zoom In / Out                |
+----------------------+------------------------------+

*/
namespace Game {
    struct CameraIntent;
    struct Camera;

    class CameraInputSystem : public ISystem {
    public:
        CameraInputSystem() = default;
        virtual ~CameraInputSystem() = default;

        CameraInputSystem(const CameraInputSystem&) = default;
        CameraInputSystem& operator=(const CameraInputSystem&) = default;

        CameraInputSystem(CameraInputSystem&&) noexcept = default;
        CameraInputSystem& operator=(CameraInputSystem&&) noexcept = default;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        enum class CameraControlMode {
            None,
            Cinematic,
            FreeLook,
            ThirdPerson
        };

        CameraControlMode ResolveMode(const Camera& Camera) const;
        void ProcessMode(CameraIntent& Intent, CameraControlMode Mode);
        void ProcessCinematicMode(CameraIntent& Intent);
        void ProcessFreeLookMode(CameraIntent& Intent);
        void ProcessThirdPersonMode(CameraIntent& Intent);
        void ProcessDefaultMode(CameraIntent& Intent);

    private:
        const std::string mName{ "CameraInputSystem" };
    };
}
