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
        OutFields.push_back(ComponentInspectionField{ "RuntimeBoneInfoOffset", std::format("{}", runtimeBoneInfoOffset) });
        OutFields.push_back(ComponentInspectionField{ "RuntimeBoneInfoCount", std::format("{}", runtimeBoneInfoCount) });

        if (model == nullptr) {
            return;
        }

        const std::vector<ModelNode>& Nodes{ model->GetNodes() };
        if (nodeIndex >= Nodes.size()) {
            return;
        }

        OutFields.push_back(ComponentInspectionField{ "BoneNodeName", Nodes[nodeIndex].GetName() });

        const std::span<const RuntimeBoneInfo> RuntimeBoneInfos{ model->GetRuntimeBoneInfos(runtimeBoneInfoOffset, runtimeBoneInfoCount) };
        for (std::size_t RuntimeBoneInfoIndex{ 0 }; RuntimeBoneInfoIndex < RuntimeBoneInfos.size(); ++RuntimeBoneInfoIndex) {
            const RuntimeBoneInfo& RuntimeBoneInfoItem{ RuntimeBoneInfos[RuntimeBoneInfoIndex] };
            OutFields.push_back(ComponentInspectionField{ std::format("BoneInfo[{}]", RuntimeBoneInfoIndex), std::format("SkinArrayIndex={}, JointArrayIndex={}", RuntimeBoneInfoItem.SkinArrayIndex, RuntimeBoneInfoItem.JointArrayIndex) });
        }
    }
}
