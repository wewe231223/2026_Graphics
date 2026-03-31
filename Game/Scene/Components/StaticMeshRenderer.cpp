#include "StaticMeshRenderer.h"
#include <format>
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* StaticMeshRenderer::GetComponentInspectionName() {
        return "StaticMeshRenderer";
    }

    void StaticMeshRenderer::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "NodeIndex", std::format("{}", nodeIndex) });
        OutFields.push_back(ComponentInspectionField{ "ModelBound", model == nullptr ? "false" : "true" });
        OutFields.push_back(ComponentInspectionField{ "Active", active ? "true" : "false" });
    }
}
