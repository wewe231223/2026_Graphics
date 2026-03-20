#include "Game/Scene/Components/Bone.h"

#include <format>

#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* Bone::GetComponentInspectionName() {
        return "Bone";
    }

    void Bone::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "ModelBound", model == nullptr ? "false" : "true" });
        OutFields.push_back(ComponentInspectionField{ "NodeIndex", std::format("{}", nodeIndex) });

        if (model == nullptr) {
            OutFields.push_back(ComponentInspectionField{ "BoneInfoCount", "0" });
            return;
        }

        const std::vector<ModelNode>& Nodes{ model->GetNodes() };
        if (nodeIndex >= Nodes.size()) {
            OutFields.push_back(ComponentInspectionField{ "BoneInfoCount", "0" });
            return;
        }

        const ModelNode& Node{ Nodes[nodeIndex] };
        const std::vector<ModelBoneInfo>& BoneInfos{ Node.GetBoneInfos() };
        OutFields.push_back(ComponentInspectionField{ "BoneInfoCount", std::format("{}", BoneInfos.size()) });

        for (std::size_t BoneInfoIndex{ 0 }; BoneInfoIndex < BoneInfos.size(); ++BoneInfoIndex) {
            const ModelBoneInfo& BoneInfo{ BoneInfos[BoneInfoIndex] };
            OutFields.push_back(ComponentInspectionField{ std::format("BoneInfo[{}]", BoneInfoIndex), std::format("SkinArrayIndex={}, JointArrayIndex={}", BoneInfo.SkinArrayIndex, BoneInfo.JointArrayIndex) });
        }
    }
}
