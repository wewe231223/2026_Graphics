#include "Input.h"

namespace Globals {
    void Input::Initialize(HWND hWnd) {
        // DirectXTK Mouse 초기화 (Window Handle 필요)
        mMouse->SetWindow(hWnd);
		mhwnd = hWnd;
    }

    void Input::Update() {
        mKeyboardState = mKeyboard->GetState();
        mMouseState = mMouse->GetState();

        if (mIsImGuiInputBlocked) {
            mKeyboardState = DirectX::Keyboard::State{};
            mMouseState = DirectX::Mouse::State{};
            mKeyboardTracker = DirectX::Keyboard::KeyboardStateTracker{};
            mMouseTracker = DirectX::Mouse::ButtonStateTracker{};
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
}
