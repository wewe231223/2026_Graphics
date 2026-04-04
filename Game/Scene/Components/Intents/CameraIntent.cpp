#include "CameraIntent.h"
#include <format>
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* CameraIntent::GetComponentInspectionName() {
        return "CameraIntent";
    }

    void CameraIntent::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "MoveDirection", std::format("{:.3f}, {:.3f}, {:.3f}", moveDirection.x, moveDirection.y, moveDirection.z) });
        OutFields.push_back(ComponentInspectionField{ "LookDelta", std::format("{:.3f}, {:.3f}", lookDelta.x, lookDelta.y) });
        OutFields.push_back(ComponentInspectionField{ "ZoomDelta", std::format("{:.3f}", zoomDelta) });
        OutFields.push_back(ComponentInspectionField{ "TargetToLockOn", std::format("index={}, generation={}, flags={}", targetToLockOn.index, targetToLockOn.generation, targetToLockOn.flags) });
        OutFields.push_back(ComponentInspectionField{ "RequestUnlock", requestUnlock ? "true" : "false" });
        OutFields.push_back(ComponentInspectionField{ "RequestSkip", requestSkip ? "true" : "false" });
        OutFields.push_back(ComponentInspectionField{ "ShakeImpulse", std::format("{:.3f}", shakeImpulse) });
    }

    void CameraIntent::Reset() {
        moveDirection = { 0.0f, 0.0f, 0.0f };
        lookDelta = { 0.0f, 0.0f };
        zoomDelta = 0.0f;

        targetToLockOn = Arche::NullEntityID;
        requestUnlock = false;
        requestSkip = false;

        shakeImpulse = 0.0f;
    }
}
