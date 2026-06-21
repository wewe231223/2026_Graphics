#pragma once

#include <array>
#include <cstdint>
#include "Utility/ComponentRestraint.h"
#include "Utility/DirectXInclude.h"
#include "Arche/Common.h"

namespace Game {
    using CameraClearColorArray = std::array<float, 4>;

    enum CameraFlag : std::uint32_t {
        CameraFlagNone = 0,
        CameraFlagInvertY = 1 << 0,
        CameraFlagFreeLook = 1 << 1,
        CameraFlagCinematic = 1 << 2,
        CameraFlagThirdPerson = 1 << 3
    };

    ComponentDecl(
        Camera,
        ComponentFields(
            ComponentField(float, fov, 60.0f)
            ComponentField(float, aspectRatio, 1.777777f)
            ComponentField(float, nearPlane, 0.1f)
            ComponentField(float, farPlane, 1000.0f)
            ComponentField(bool, isActive, true)
            ComponentField(bool, isOrthographic, false)
            ComponentField(float, orthoSize, 5.0f)
            ComponentField(CameraClearColorArray, clearColor, {})
            ComponentField(Arche::EntityID, thirdPersonFollowTarget, Arche::NullEntityID)
            ComponentField(std::int64_t, thirdPersonFollowTargetSerializedId, -1)
            ComponentField(float, thirdPersonDistance, 4.0f)
            ComponentField(float, thirdPersonMinDistance, 1.5f)
            ComponentField(float, thirdPersonMaxDistance, 8.0f)
            ComponentField(float, thirdPersonHeightOffset, 1.6f)
            ComponentField(float, thirdPersonOrbitYaw, 0.0f)
            ComponentField(float, thirdPersonOrbitPitch, 0.25f)
            ComponentField(float, thirdPersonPositionLerpSpeed, 12.0f)
            ComponentField(float, thirdPersonZoomSpeed, 0.5f)
            ComponentField(std::uint32_t, cameraFlags, CameraFlagFreeLook)
            ComponentField(SimpleMath::Matrix, viewMatrix, SimpleMath::Matrix::Identity)
            ComponentField(SimpleMath::Matrix, projMatrix, SimpleMath::Matrix::Identity)
        ),
        BOOST_PP_SEQ_NIL
    );
}
