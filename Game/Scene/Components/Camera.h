#pragma once 

#include <cstdint>
#include "Utility/ComponentRestraint.h"
#include "Utility/DirectXInclude.h"
#include "Arche/Common.h"

namespace Game {
    enum CameraFlag : std::uint32_t {
        CameraFlagNone = 0,
        CameraFlagInvertY = 1 << 0, ///< Y축 입력 반전
        CameraFlagFreeLook = 1 << 1, ///< 캐릭터 방향과 카메라 회전 분리
        CameraFlagCinematic = 1 << 2, ///< 연출 상태 (입력 무시)
        CameraFlagThirdPerson = 1 << 3  ///< 3인칭 모드 여부
    };

    Component(Camera)
        float fov{ 60.0f };
        float aspectRatio{ 1.777777f }; // 16:9
        float nearPlane{ 0.1f };
        float farPlane{ 1000.0f };

        bool isActive{ true };
        bool isOrthographic{ false };
        float orthoSize{ 5.0f };             // 직교 투영 시 세로 절반 크기
        float clearColor[4]{ 0.1f, 0.1f, 0.1f, 1.0f };
        Arche::EntityID thirdPersonFollowTarget{ Arche::NullEntityID };
        std::int64_t thirdPersonFollowTargetSerializedId{ -1 };
        float thirdPersonDistance{ 4.0f };
        float thirdPersonMinDistance{ 1.5f };
        float thirdPersonMaxDistance{ 8.0f };
        float thirdPersonHeightOffset{ 1.6f };
        float thirdPersonOrbitYaw{ 0.0f };
        float thirdPersonOrbitPitch{ 0.25f };
        float thirdPersonPositionLerpSpeed{ 12.0f };
        float thirdPersonZoomSpeed{ 0.5f };

        std::uint32_t cameraFlags{ CameraFlagFreeLook };

        SimpleMath::Matrix viewMatrix = SimpleMath::Matrix::Identity;
        SimpleMath::Matrix projMatrix = SimpleMath::Matrix::Identity;
    EndComponent(Camera)
}
