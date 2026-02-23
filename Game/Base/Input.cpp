#include "Input.h"

namespace Globals {
    void Input::Initialize(HWND hWnd) {
        // DirectXTK Mouse 초기화 (Window Handle 필요)
        mMouse->SetWindow(hWnd);
    }

    void Input::Update() {
        mKeyboardState = mKeyboard->GetState();
        mMouseState = mMouse->GetState();

        mKeyboardTracker.Update(mKeyboardState);
        mMouseTracker.Update(mMouseState);
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
    bool Input::IsKeyDown(DirectX::Keyboard::Keys key) const {
		return mKeyboardState.IsKeyDown(key);
    }
    bool Input::IsKeyPressed(DirectX::Keyboard::Keys key) const {
        return mKeyboardTracker.IsKeyPressed(key);
    }
    bool Input::IsKeyReleased(DirectX::Keyboard::Keys key) const {
		return mKeyboardTracker.IsKeyReleased(key);
    }
}