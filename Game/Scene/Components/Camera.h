#pragma once 

#include "Utility/ComponentRestraint.h"

namespace Game {
	Component(Camera)
        float fov{ 60.0f };
        float aspectRatio{ 1.777777f }; // 16:9
        float nearPlane{ 0.1f };
        float farPlane{ 1000.0f };

        bool isActive{ true };
        bool isOrthographic{ false };
        float orthoSize{ 5.0f };       // 직교 투영 시 세로 절반 크기
        uint32_t cullingMask{ 0xFFFFFFFF }; // 모든 레이어 렌더링

        float clearColor[4]{ 0.1f, 0.1f, 0.1f, 1.0f }; // RGBA
        int32_t priority{ 0 };         

        // RenderTargetID targetTexture = INVALID_ID;
	EndComponent(Camera)
}
