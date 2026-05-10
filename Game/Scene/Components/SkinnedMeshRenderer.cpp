#include "SkinnedMeshRenderer.h"
#include <format>
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* SkinnedMeshRenderer::GetComponentInspectionName() {
        return "SkinnedMeshRenderer";
    }

    void SkinnedMeshRenderer::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "NodeIndex", std::format("{}", nodeIndex) });
        OutFields.push_back(ComponentInspectionField{ "ModelBound", model == nullptr ? "false" : "true" });
        OutFields.push_back(ComponentInspectionField{ "Active", active ? "true" : "false" });
    }
}
