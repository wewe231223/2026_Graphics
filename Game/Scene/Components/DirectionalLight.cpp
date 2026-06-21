#include "DirectionalLight.h"
#include <format>
#include <string>
#include "Game/Scene/Components/ComponentInspection.h"

namespace {
    std::string FormatVector3(const DirectX::SimpleMath::Vector3& Value) {
        return std::format("{:.3f}, {:.3f}, {:.3f}", Value.x, Value.y, Value.z);
    }
}

namespace Game {
    const char* DirectionalLight::GetComponentInspectionName() {
        return "DirectionalLight";
    }

    void DirectionalLight::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "IsActive", mIsActive ? "true" : "false" });
        OutFields.push_back(ComponentInspectionField{ "CastsShadow", mCastsShadow ? "true" : "false" });
        OutFields.push_back(ComponentInspectionField{ "UseTransformDirection", mUseTransformDirection ? "true" : "false" });
        OutFields.push_back(ComponentInspectionField{ "Direction", FormatVector3(mDirection) });
        OutFields.push_back(ComponentInspectionField{ "Color", FormatVector3(mColor) });
        OutFields.push_back(ComponentInspectionField{ "Intensity", std::format("{:.3f}", mIntensity) });
        OutFields.push_back(ComponentInspectionField{ "AmbientIntensity", std::format("{:.3f}", mAmbientIntensity) });
    }
}
