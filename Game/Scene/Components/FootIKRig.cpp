#include "Game/Scene/Components/FootIKRig.h"

#include <string>
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    void SetFootIKRigBoneName(ComponentTextArray& TargetText, const std::string_view SourceText) {
        TargetText.Assign(SourceText);
    }

    std::string_view GetFootIKRigBoneNameText(const ComponentTextArray& SourceText) {
        return SourceText.AsStringView();
    }

    const char* FootIKRig::GetComponentInspectionName() {
        return "FootIKRig";
    }

    void FootIKRig::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        const std::string_view LeftFootBoneNameText{ GetFootIKRigBoneNameText(mLeftFootBoneName) };
        const std::string_view RightFootBoneNameText{ GetFootIKRigBoneNameText(mRightFootBoneName) };
        const std::string_view LeftToeBoneNameText{ GetFootIKRigBoneNameText(mLeftToeBoneName) };
        const std::string_view RightToeBoneNameText{ GetFootIKRigBoneNameText(mRightToeBoneName) };
        const std::string_view LeftShinBoneNameText{ GetFootIKRigBoneNameText(mLeftShinBoneName) };
        const std::string_view RightShinBoneNameText{ GetFootIKRigBoneNameText(mRightShinBoneName) };
        const std::string_view LeftThighBoneNameText{ GetFootIKRigBoneNameText(mLeftThighBoneName) };
        const std::string_view RightThighBoneNameText{ GetFootIKRigBoneNameText(mRightThighBoneName) };
        const std::string_view PelvisBoneNameText{ GetFootIKRigBoneNameText(mPelvisBoneName) };
        const bool HasLeftFootBoneName{ LeftFootBoneNameText.empty() == false };
        const bool HasRightFootBoneName{ RightFootBoneNameText.empty() == false };
        const bool HasLeftToeBoneName{ LeftToeBoneNameText.empty() == false };
        const bool HasRightToeBoneName{ RightToeBoneNameText.empty() == false };
        const bool HasLeftShinBoneName{ LeftShinBoneNameText.empty() == false };
        const bool HasRightShinBoneName{ RightShinBoneNameText.empty() == false };
        const bool HasLeftThighBoneName{ LeftThighBoneNameText.empty() == false };
        const bool HasRightThighBoneName{ RightThighBoneNameText.empty() == false };
        const bool HasPelvisBoneName{ PelvisBoneNameText.empty() == false };

        OutFields.push_back(ComponentInspectionField{ "Enabled", mEnabled ? "true" : "false" });
        OutFields.push_back(ComponentInspectionField{ "LeftFootBoneName", HasLeftFootBoneName ? std::string{ LeftFootBoneNameText } : "<None>" });
        OutFields.push_back(ComponentInspectionField{ "RightFootBoneName", HasRightFootBoneName ? std::string{ RightFootBoneNameText } : "<None>" });
        OutFields.push_back(ComponentInspectionField{ "LeftToeBoneName", HasLeftToeBoneName ? std::string{ LeftToeBoneNameText } : "<None>" });
        OutFields.push_back(ComponentInspectionField{ "RightToeBoneName", HasRightToeBoneName ? std::string{ RightToeBoneNameText } : "<None>" });
        OutFields.push_back(ComponentInspectionField{ "LeftShinBoneName", HasLeftShinBoneName ? std::string{ LeftShinBoneNameText } : "<None>" });
        OutFields.push_back(ComponentInspectionField{ "RightShinBoneName", HasRightShinBoneName ? std::string{ RightShinBoneNameText } : "<None>" });
        OutFields.push_back(ComponentInspectionField{ "LeftThighBoneName", HasLeftThighBoneName ? std::string{ LeftThighBoneNameText } : "<None>" });
        OutFields.push_back(ComponentInspectionField{ "RightThighBoneName", HasRightThighBoneName ? std::string{ RightThighBoneNameText } : "<None>" });
        OutFields.push_back(ComponentInspectionField{ "PelvisBoneName", HasPelvisBoneName ? std::string{ PelvisBoneNameText } : "<None>" });
        OutFields.push_back(ComponentInspectionField{ "BlendSpeed", std::to_string(mBlendSpeed) });
        OutFields.push_back(ComponentInspectionField{ "MaxLift", std::to_string(mMaxLift) });
        OutFields.push_back(ComponentInspectionField{ "MaxDrop", std::to_string(mMaxDrop) });
    }
}
