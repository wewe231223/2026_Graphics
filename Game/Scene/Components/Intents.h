#pragma once 
#include <cstdint>
#include "Utility/ComponentRestraint.h"
#include "Utility/DirectXInclude.h"
#include "Arche/Common.h"

/**
 * @file CameraIntent.h
 * @brief 유저의 입력 및 연출 의도를 엔진 카메라 시스템에 전달하는 컴포넌트입니다.
 * * [ VISUAL STRUCTURE ]
 * ===========================================================================================
 * * (Move)          (Look)          (Zoom)             [ Target ID ] ----> ( Tracking )
 * W A S D         / \             [ + ]              (Arche::Entity)      (isLockOnActive)
 * Q / E     <--  ( O )  -->       [ - ]
 * [3D Vec]        \ /             [Float]            [ Bitwise Flags ] -> [Invert/Free/Cine]
 * * ===========================================================================================
 */

namespace Game {
    Component(CameraIntent)
        // ---------------------------------------------------------------------------------------
        /** @name 1. 기본 조작 데이터 (Raw Input Mapping)
        * 매 프레임 Reset()을 통해 초기화되는 휘발성 입력 데이터입니다.
        * @{ */

        /// @brief WASD, Q/E 등을 통한 3축 이동 의도 (Local/World 구분은 시스템에서 처리)
        SimpleMath::Vector3 moveDirection = { 0.0f, 0.0f, 0.0f };

        /// @brief 마우스 델타나 스틱 회전 값 (x: Yaw, y: Pitch)
        SimpleMath::Vector2 lookDelta = { 0.0f, 0.0f };

        /// @brief 마우스 휠 또는 트리거를 통한 줌 인/아웃 의도
        float zoomDelta = 0.0f;
        /** @} */


        // ---------------------------------------------------------------------------------------
        /** @name 2. 타겟팅 및 락온 (Targeting)
         * 특정 대상이나 지점을 고정해서 바라볼 때 사용합니다.
         * @{ */

         /// @brief 추적할 엔티티 ID
        Arche::EntityID lockOnTarget{ Arche::NullEntityID };

        /// @brief 월드 상의 특정 좌표를 고정해서 바라볼 때의 위치값
        SimpleMath::Vector3 lookAtPosition = { 0.0f, 0.0f, 0.0f };

        /// @brief 현재 타겟팅(Lock-On) 로직의 활성화 여부
        bool isLockOnActive = false;
        /** @} */


        // ---------------------------------------------------------------------------------------
        /** @name 3. 연출 및 보정 파라미터 (Dynamic Controls)
         * 카메라의 움직임에 부드러움이나 특수 효과를 더합니다.
         * @{ */

         /// @brief 카메라 추적 속도 조절 (0.0: 즉시 대응 ~ 1.0: 매우 느리고 부드럽게)
        float smoothingFactor = 0.1f;

        /// @brief 질주나 스킬 사용 시 기본 FOV에 더해질 오프셋 값
        float fovBias = 0.0f;

        /// @brief 폭발이나 피격 시 주입되는 흔들림 강도
        float shakeIntensity = 0.0f;
        /** @} */


        // ---------------------------------------------------------------------------------------
        /** @name 4. 상태 제어 플래그 (State Flags)
         * 비트 연산을 통해 카메라의 동작 모드를 제어합니다.
         * @{ */

        enum Flags : uint32_t {
            None = 0,
            InvertY = 1 << 0, ///< Y축 입력 반전 여부
            FreeLook = 1 << 1, ///< 캐릭터 방향과 카메라 방향의 분리 여부
            Cinematic = 1 << 2, ///< 연출 상태 (유저 입력 차단)
            ThirdPerson = 1 << 3  ///< 1인칭(OFF) / 3인칭(ON) 전환 의도
        };

        /// @brief 현재 적용된 카메라 동작 플래그 비트마스크
        uint32_t cameraFlags = Flags::None;
        /** @} */


        // ---------------------------------------------------------------------------------------
        /**
         * @brief 매 프레임 소모되어야 하는 델타성 데이터들을 초기화합니다.
         * @details GenericCleanUpSystem 등에 의해 정적 바인딩으로 호출됩니다.
         * @note @b shakeIntensity 나 @b lockOnTarget 은 상태 유지를 위해 초기화에서 제외됩니다.
         */
        void Reset() {
            moveDirection = { 0.0f, 0.0f, 0.0f };
            lookDelta = { 0.0f, 0.0f };
            zoomDelta = 0.0f;

            // shakeIntensity는 감쇄 시스템이 없다면 여기서 0으로 밀어줄 수 있습니다.
        }

    EndComponent(CameraIntent)
}