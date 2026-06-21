#include "Camera.h"
#include <format>
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* Camera::GetComponentInspectionName() {
        return "Camera";
    }

    void Camera::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "Fov", std::format("{:.3f}", fov) });
        OutFields.push_back(ComponentInspectionField{ "AspectRatio", std::format("{:.3f}", aspectRatio) });
        OutFields.push_back(ComponentInspectionField{ "NearPlane", std::format("{:.3f}", nearPlane) });
        OutFields.push_back(ComponentInspectionField{ "FarPlane", std::format("{:.3f}", farPlane) });
        OutFields.push_back(ComponentInspectionField{ "IsActive", isActive ? "true" : "false" });
        OutFields.push_back(ComponentInspectionField{ "ThirdPersonFollowTarget", std::format("{}:{}", thirdPersonFollowTarget.index, thirdPersonFollowTarget.generation) });
        OutFields.push_back(ComponentInspectionField{ "ThirdPersonDistance", std::format("{:.3f}", thirdPersonDistance) });
        OutFields.push_back(ComponentInspectionField{ "ThirdPersonOrbitYaw", std::format("{:.3f}", thirdPersonOrbitYaw) });
        OutFields.push_back(ComponentInspectionField{ "ThirdPersonOrbitPitch", std::format("{:.3f}", thirdPersonOrbitPitch) });
    }
}
