#pragma once 
#include <cstdint>
#include "Utility/ComponentRestraint.h"
#include "Utility/DirectXInclude.h"
#include "Arche/Common.h"

/**
 * @file CameraIntent.h
 * @brief 유저나 AI, 연출 시스템이 '이번 프레임'에 카메라에게 요구하는 휘발성 명령입니다.
 */

namespace Game {
    Component(CameraIntent)
        // ---------------------------------------------------------------------------------------
        /** @name 1. 휘발성 조작 데이터 (Transient Input)
        * 매 프레임 Reset()을 통해 0으로 초기화되는 순수 입력 델타입니다.
        * @{ */

        /// @brief 이동 의도 (WASD, Q/E 등)
        SimpleMath::Vector3 moveDirection { 0.0f, 0.0f, 0.0f };

        /// @brief 회전 의도 (Mouse Delta, Stick)
        SimpleMath::Vector2 lookDelta { 0.0f, 0.0f };

        /// @brief 줌 의도 (Mouse Wheel, Trigger)
        float zoomDelta{ 0.0f };
        /** @} */


        // ---------------------------------------------------------------------------------------
        /** @name 2. 일시적 요청 (Action Requests)
         * 상태를 바꾸거나 특정 타겟을 지정하려는 '순간적인' 시도입니다.
         * @{ */

         /// @brief 이번 프레임에 새롭게 추적하고자 하는 엔티티 (변경 시에만 주입)
        Arche::EntityID targetToLockOn{ Arche::NullEntityID };

        /// @brief 락온 해제 요청 여부
        bool requestUnlock{ false };

        /// @brief 시네마틱 스킵 등 일회성 이벤트 요청
        bool requestSkip { false };
        /** @} */


        // ---------------------------------------------------------------------------------------
        /** @name 3. 외부 주입 데이터 (External Injection)
         * 폭발 시스템 등이 이번 프레임에 한해서만 주입하는 충격량입니다.
         * @{ */

         /// @brief 이번 프레임에 추가되는 즉각적인 흔들림 강도
        float shakeImpulse{ 0.0f };
        /** @} */


        /**
         * @brief 다음 프레임을 위해 모든 의도 데이터를 초기화합니다.
         */
        void Reset() {
            moveDirection = { 0.0f, 0.0f, 0.0f };
            lookDelta = { 0.0f, 0.0f };
            zoomDelta = 0.0f;

            targetToLockOn = Arche::NullEntityID;
            requestUnlock = false;
            requestSkip = false;

            shakeImpulse = 0.0f;
        }

    EndComponent(CameraIntent)
}
