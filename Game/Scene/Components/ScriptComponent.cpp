#include "ScriptComponent.h"

#include <string>

#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    void SetScriptFileName(BehaviorInstanceComponent& TargetComponent, const std::string_view FileName) {
        TargetComponent.mScriptFileName.Assign(FileName);
    }

    std::string_view GetScriptFileNameView(const BehaviorInstanceComponent& TargetComponent) {
        return TargetComponent.mScriptFileName.AsStringView();
    }

    const char* GetScriptFileName(const BehaviorInstanceComponent& TargetComponent) {
        return TargetComponent.mScriptFileName.data();
    }

    const char* BehaviorInstanceComponent::GetComponentInspectionName() {
        return "ScriptComponent";
    }

    void BehaviorInstanceComponent::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        const std::string_view ScriptFileName{ GetScriptFileNameView(*this) };
        const bool HasScriptFileName{ ScriptFileName.empty() == false };
        OutFields.push_back(ComponentInspectionField{ "ScriptFileName", HasScriptFileName ? std::string{ ScriptFileName } : "<None>" });
        OutFields.push_back(ComponentInspectionField{ "BehaviorAssetId", std::to_string(mBehaviorAssetId) });
    }
}
