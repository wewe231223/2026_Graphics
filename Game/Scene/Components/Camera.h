#pragma once 

#include <cstdint>
#include "Utility/ComponentRestraint.h"
#include "Utility/DirectXInclude.h"
#include "Arche/Common.h"

namespace Game {
    Component(Camera)
        // ---------------------------------------------------------------------------------------
        /** @name 1. 투영 및 렌더링 설정 (Projection & Rendering)
         * 광학적 특성과 렌더 파이프라인 제어를 위한 설정입니다. */

        float fov{ 60.0f };
        float aspectRatio{ 1.777777f }; // 16:9
        float nearPlane{ 0.1f };
        float farPlane{ 1000.0f };

        bool isActive{ true };
        bool isOrthographic{ false };
        float orthoSize{ 5.0f };             // 직교 투영 시 세로 절반 크기
        uint32_t cullingMask{ 0xFFFFFFFF };  // 모든 레이어 렌더링

        float clearColor[4]{ 0.1f, 0.1f, 0.1f, 1.0f }; // RGBA
        int32_t priority{ 0 };               // 여러 카메라가 있을 때의 렌더링 우선순위

        // RenderTargetID targetTexture = INVALID_ID;


        // ---------------------------------------------------------------------------------------
        /** @name 2. 카메라 동작 상태 및 보정 (Movement State & Smoothing)
         * CameraIntent로부터 전달받은 명령을 처리할 때 참조하는 지속적 상태입니다. */

         /// @brief 카메라 회전/이동의 부드러움 정도 (0: 즉시 ~ 1: 매우 느림)
        float smoothingFactor{ 0.1f };

        /// @brief 현재 적용 중인 FOV 오프셋 (질주 시 확대 등)
        float currentFovBias{ 0.0f };

        /// @brief 현재 누적된 흔들림 강도 (매 프레임 시스템에서 감쇄 처리)
        float currentShakeIntensity{ 0.0f };


        // ---------------------------------------------------------------------------------------
        /** @name 3. 관계 및 모드 제어 (Targeting & Flags)
         * 특정 대상을 추적하거나 카메라의 고유 동작 모드를 유지합니다. */

         /// @brief 현재 추적 중인 대상 엔티티
        Arche::EntityID lockOnTarget{ Arche::NullEntityID };

        enum Flags : uint32_t {
            None = 0,
            InvertY = 1 << 0, ///< Y축 입력 반전
            FreeLook = 1 << 1, ///< 캐릭터 방향과 카메라 회전 분리
            Cinematic = 1 << 2, ///< 연출 상태 (입력 무시)
            ThirdPerson = 1 << 3  ///< 3인칭 모드 여부
        };

        /// @brief 현재 카메라에 설정된 동작 플래그
        uint32_t cameraFlags{ Flags::FreeLook };


        // ---------------------------------------------------------------------------------------
        /** @name 4. 결과 행렬 (Cached Matrices)
         * 매 프레임 계산되어 GPU로 전송될 최종 행렬 데이터입니다. */

        SimpleMath::Matrix viewMatrix = SimpleMath::Matrix::Identity;
        SimpleMath::Matrix projMatrix = SimpleMath::Matrix::Identity;
    EndComponent(Camera)
}