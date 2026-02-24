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

        Input::UpdateCursor(); 

        mKeyboardTracker.Update(mKeyboardState);
        mMouseTracker.Update(mMouseState);
    }

    void Input::Terminate() {
        if (mVirtualMouse) {
			SetVirtualMouse(false);
        }
    }

    void Input::SetVirtualMouse(bool enable) {
		mVirtualMouse = enable;

        if (enable) {
            ShowCursor(false);

            RECT clientRect;
            GetClientRect(mhwnd, &clientRect);

            mVirtualMousePosition = { clientRect.right / 2, clientRect.bottom / 2 };

            ClientToScreen(mhwnd, &mVirtualMousePosition);
        }
		else {
			ShowCursor(true);
		}
    }


    void Input::ToggleVirtualMouse() {
        Input::SetVirtualMouse(not mVirtualMouse);
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

    void Input::UpdateCursor() {
        if (mVirtualMouse) {
			SetCursorPos(mVirtualMousePosition.x, mVirtualMousePosition.y);
        }
    }
}