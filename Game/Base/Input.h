#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <memory>
#include "External/Include/DirectXTK12/Keyboard.h"
#include "External/Include/DirectXTK12/Mouse.h"

namespace Globals {
    class Input {
        Input() = default;
        ~Input() = default;
    public:
        Input(const Input&) = delete;
        Input& operator=(const Input&) = delete;

        Input(Input&&) = delete;
        Input& operator=(Input&&) = delete;
    public:
        static Input& Get() {
            static Input instance;
            return instance;
        }

    public:
        void Initialize(HWND hWnd);
        void Update();
        void Terminate(); 
        void SetImGuiInputBlocked(bool IsBlocked);
        void SetWindowFocused(bool IsFocused);

		void SetVirtualMouse(bool enable);
		void ToggleVirtualMouse();
        void SetRightButtonVirtualMouseEnabled(bool IsEnabled);

        const DirectX::Keyboard::State& GetKeyboardState() const;
        const DirectX::Mouse::State& GetMouseState() const;
        const DirectX::Keyboard::KeyboardStateTracker& GetKeyboardTracker() const;
        const DirectX::Mouse::ButtonStateTracker& GetMouseTracker() const;

        float GetMouseDeltaX() const; 
		float GetMouseDeltaY() const;
        std::int32_t GetMouseWheelDelta() const;

        bool IsKeyDown(DirectX::Keyboard::Keys key) const;
        bool IsKeyPressed(DirectX::Keyboard::Keys key) const;
        bool IsKeyReleased(DirectX::Keyboard::Keys key) const;
        bool IsImGuiInputBlocked() const;

    private:
        bool IsWindowFocusedByOperatingSystem() const;
        void ResetInputState();
        void UpdateCursor(); 

    private:
        std::unique_ptr<DirectX::Keyboard> mKeyboard{ std::make_unique<DirectX::Keyboard>() };
        std::unique_ptr<DirectX::Mouse> mMouse{ std::make_unique<DirectX::Mouse>() };

        DirectX::Keyboard::State mKeyboardState{};
        DirectX::Mouse::State mMouseState{};

        DirectX::Keyboard::KeyboardStateTracker mKeyboardTracker{};
        DirectX::Mouse::ButtonStateTracker mMouseTracker{};

        HWND mhwnd{ nullptr };
        bool mVirtualMouse{ false };
        bool mRightButtonVirtualMouseEnabled{ true };
        bool mIsImGuiInputBlocked{ false };
        bool mIsWindowFocused{ true };
        POINT mVirtualMousePosition{};
        std::int32_t mPreviousScrollWheelValue{};
        std::int32_t mMouseWheelDelta{};
    };

    bool IsInputKeyDown(std::int32_t KeyCode);
    bool IsInputKeyPressed(std::int32_t KeyCode);
    bool IsInputKeyReleased(std::int32_t KeyCode);

    std::int32_t GetInputMousePositionX();
    std::int32_t GetInputMousePositionY();

    float GetInputMouseDeltaX();
    float GetInputMouseDeltaY();

    bool IsInputMouseLeftButtonDown();
    bool IsInputMouseRightButtonDown();
    bool IsInputMouseMiddleButtonDown();
    std::int32_t GetInputMouseWheelDelta();
}
