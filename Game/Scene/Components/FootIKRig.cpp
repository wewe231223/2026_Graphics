#include "Game/Scene/Components/FootIKRig.h"

#include <algorithm>
#include <string>
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    void SetFootIKRigBoneName(ComponentTextArray& TargetText, const ::std::string_view SourceText) {
        TargetText.fill('\0');

        const ::std::size_t CopyLength{ ::std::min(SourceText.size(), ComponentTextMaxLength) };
        for (::std::size_t Index{ 0 }; Index < CopyLength; ++Index) {
            TargetText[Index] = SourceText[Index];
        }
    }

    const char* GetFootIKRigBoneNameText(const ComponentTextArray& SourceText) {
        return SourceText.data();
    }

    const char* FootIKRig::GetComponentInspectionName() {
        return "FootIKRig";
    }

    void FootIKRig::BuildComponentInspectionFields(::std::vector<ComponentInspectionField>& OutFields) const {
        const char* LeftFootBoneNameText{ GetFootIKRigBoneNameText(mLeftFootBoneName) };
        const char* RightFootBoneNameText{ GetFootIKRigBoneNameText(mRightFootBoneName) };
        const bool HasLeftFootBoneName{ LeftFootBoneNameText != nullptr && LeftFootBoneNameText[0] != '\0' };
        const bool HasRightFootBoneName{ RightFootBoneNameText != nullptr && RightFootBoneNameText[0] != '\0' };

        OutFields.push_back(ComponentInspectionField{ "Enabled", mEnabled ? "true" : "false" });
        OutFields.push_back(ComponentInspectionField{ "LeftFootBoneName", HasLeftFootBoneName ? LeftFootBoneNameText : "<None>" });
        OutFields.push_back(ComponentInspectionField{ "RightFootBoneName", HasRightFootBoneName ? RightFootBoneNameText : "<None>" });
        OutFields.push_back(ComponentInspectionField{ "FootSoleOffset", ::std::to_string(mFootSoleOffset) });
        OutFields.push_back(ComponentInspectionField{ "BlendSpeed", ::std::to_string(mBlendSpeed) });
        OutFields.push_back(ComponentInspectionField{ "MaxLift", ::std::to_string(mMaxLift) });
    }
}
