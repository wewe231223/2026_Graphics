#include "ScriptComponent.h"

#include <algorithm>
#include <string>

#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    void SetScriptFileName(BehaviorInstanceComponent& TargetComponent, const ::std::string_view FileName) {
        TargetComponent.mScriptFileName.fill('\0');

        const ::std::size_t CopyLength{ ::std::min(FileName.size(), ComponentTextMaxLength) };
        for (::std::size_t Index{ 0 }; Index < CopyLength; ++Index) {
            TargetComponent.mScriptFileName[Index] = FileName[Index];
        }
    }

    const char* GetScriptFileName(const BehaviorInstanceComponent& TargetComponent) {
        return TargetComponent.mScriptFileName.data();
    }

    const char* BehaviorInstanceComponent::GetComponentInspectionName() {
        return "ScriptComponent";
    }

    void BehaviorInstanceComponent::BuildComponentInspectionFields(::std::vector<ComponentInspectionField>& OutFields) const {
        const char* ScriptFileName{ GetScriptFileName(*this) };
        const bool HasScriptFileName{ ScriptFileName != nullptr && ScriptFileName[0] != '\0' };
        OutFields.push_back(ComponentInspectionField{ "ScriptFileName", HasScriptFileName ? ScriptFileName : "<None>" });
        OutFields.push_back(ComponentInspectionField{ "BehaviorAssetId", ::std::to_string(mBehaviorAssetId) });
    }
}
