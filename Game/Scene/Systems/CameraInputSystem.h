#pragma once
#include <string>
#include <vector>
#include "Game/Scene/System.h"


namespace Game {

    /**
     * @class CameraInputSystem
     * @brief 하드웨어 입력을 읽어 CameraIntent 컴포넌트를 채우는 시스템입니다.
     */
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
        void ProcessKeyboard(struct CameraIntent& intent);
        void ProcessMouse(struct CameraIntent& intent);

    private:
		const std::string mName{ "CameraInputSystem" };
    };

}