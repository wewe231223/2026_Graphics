#include "Input.h"

namespace {
    bool TryConvertKeyCode(std::int32_t KeyCode, DirectX::Keyboard::Keys& OutKey) {
        if (KeyCode < static_cast<std::int32_t>(DirectX::Keyboard::Keys::None)) {
            return false;
        }

        if (KeyCode > static_cast<std::int32_t>(DirectX::Keyboard::Keys::OemClear)) {
            return false;
        }

        OutKey = static_cast<DirectX::Keyboard::Keys>(KeyCode);
        return true;
    }
}

namespace Globals {
    void Input::Initialize(HWND hWnd) {
        // DirectXTK Mouse 초기화 (Window Handle 필요)
        mMouse->SetWindow(hWnd);
		mhwnd = hWnd;
    }

    void Input::Update() {
        mKeyboardState = mKeyboard->GetState();
        mMouseState = mMouse->GetState();
        mMouseWheelDelta = mMouseState.scrollWheelValue - mPreviousScrollWheelValue;
        mPreviousScrollWheelValue = mMouseState.scrollWheelValue;

        if (mIsImGuiInputBlocked) {
            mKeyboardState = DirectX::Keyboard::State{};
            mMouseState = DirectX::Mouse::State{};
            mKeyboardTracker = DirectX::Keyboard::KeyboardStateTracker{};
            mMouseTracker = DirectX::Mouse::ButtonStateTracker{};
            mMouseWheelDelta = 0;
            return;
        }

        mKeyboardTracker.Update(mKeyboardState);
        mMouseTracker.Update(mMouseState);

        Input::UpdateCursor(); 
    }

    void Input::Terminate() {
        if (mVirtualMouse) {
			SetVirtualMouse(false);
        }
    }

    void Input::SetImGuiInputBlocked(bool IsBlocked) {
        mIsImGuiInputBlocked = IsBlocked;
    }

    void Input::SetVirtualMouse(bool enable) {
        if (mVirtualMouse == enable) {
            return; 
        }

		mVirtualMouse = enable;

        if (enable) {
            ShowCursor(false);

            RECT clientRect;
            GetClientRect(mhwnd, &clientRect);

            mVirtualMousePosition = { clientRect.right / 2, clientRect.bottom / 2 };

            POINT virtualMouseScreenPosition{ mVirtualMousePosition };
            ClientToScreen(mhwnd, &virtualMouseScreenPosition);
            SetCursorPos(virtualMouseScreenPosition.x, virtualMouseScreenPosition.y);

            mMouseState.x = mVirtualMousePosition.x;
            mMouseState.y = mVirtualMousePosition.y;
        }
		else {
			ShowCursor(true);
		}
    }


    void Input::ToggleVirtualMouse() {
        Input::SetVirtualMouse(not mVirtualMouse);
    }

    void Input::SetRightButtonVirtualMouseEnabled(bool IsEnabled) {
        mRightButtonVirtualMouseEnabled = IsEnabled;
    }


    const DirectX::Keyboard::State& Input::GetKeyboardState() const {
        return mKeyboardState;
    }

    const DirectX::Mouse::State& Input::GetMouseState() const {
        return mMouseState;
    }

    const DirectX::Keyboard::KeyboardStateTracker& Input::GetKeyboardTracker() const {
        return mKeyboardTracker; 
    }

    const DirectX::Mouse::ButtonStateTracker& Input::GetMouseTracker() const {
        return mMouseTracker; 
    }

    float Input::GetMouseDeltaX() const {
        if (not mVirtualMouse) {
            return 0.f; 
        }

		return static_cast<float>(mMouseState.x - mVirtualMousePosition.x);
    }

    float Input::GetMouseDeltaY() const {
		if (not mVirtualMouse) {
			return 0.f;
		}

		return static_cast<float>(mMouseState.y - mVirtualMousePosition.y);
    }

    bool Input::IsKeyDown(DirectX::Keyboard::Keys key) const {
		return mKeyboardState.IsKeyDown(key);
    }

    bool Input::IsKeyPressed(DirectX::Keyboard::Keys key) const {
        return mKeyboardTracker.IsKeyPressed(key);
    }

    bool Input::IsKeyReleased(DirectX::Keyboard::Keys key) const {
		return mKeyboardTracker.IsKeyReleased(key);
    }

    std::int32_t Input::GetMouseWheelDelta() const {
        return mMouseWheelDelta;
    }

    bool Input::IsImGuiInputBlocked() const {
        return mIsImGuiInputBlocked;
    }

    void Input::UpdateCursor() {
        if (mRightButtonVirtualMouseEnabled) {
            Input::SetVirtualMouse(mMouseTracker.rightButton);
        }

        if (mVirtualMouse) {
            POINT virtualMouseScreenPosition{ mVirtualMousePosition };
            ClientToScreen(mhwnd, &virtualMouseScreenPosition);

			SetCursorPos(virtualMouseScreenPosition.x, virtualMouseScreenPosition.y);
        }
    }

    bool IsInputKeyDown(std::int32_t KeyCode) {
        DirectX::Keyboard::Keys TargetKey{ DirectX::Keyboard::Keys::None };
        const bool IsValidKeyCode{ TryConvertKeyCode(KeyCode, TargetKey) };
        if (IsValidKeyCode == false) {
            return false;
        }

        return Input::Get().IsKeyDown(TargetKey);
    }

    bool IsInputKeyPressed(std::int32_t KeyCode) {
        DirectX::Keyboard::Keys TargetKey{ DirectX::Keyboard::Keys::None };
        const bool IsValidKeyCode{ TryConvertKeyCode(KeyCode, TargetKey) };
        if (IsValidKeyCode == false) {
            return false;
        }

        return Input::Get().IsKeyPressed(TargetKey);
    }

    bool IsInputKeyReleased(std::int32_t KeyCode) {
        DirectX::Keyboard::Keys TargetKey{ DirectX::Keyboard::Keys::None };
        const bool IsValidKeyCode{ TryConvertKeyCode(KeyCode, TargetKey) };
        if (IsValidKeyCode == false) {
            return false;
        }

        return Input::Get().IsKeyReleased(TargetKey);
    }

    std::int32_t GetInputMousePositionX() {
        return Input::Get().GetMouseState().x;
    }

    std::int32_t GetInputMousePositionY() {
        return Input::Get().GetMouseState().y;
    }

    float GetInputMouseDeltaX() {
        return Input::Get().GetMouseDeltaX();
    }

    float GetInputMouseDeltaY() {
        return Input::Get().GetMouseDeltaY();
    }

    bool IsInputMouseLeftButtonDown() {
        return Input::Get().GetMouseState().leftButton;
    }

    bool IsInputMouseRightButtonDown() {
        return Input::Get().GetMouseState().rightButton;
    }

    bool IsInputMouseMiddleButtonDown() {
        return Input::Get().GetMouseState().middleButton;
    }

    std::int32_t GetInputMouseWheelDelta() {
        return Input::Get().GetMouseWheelDelta();
    }
}
