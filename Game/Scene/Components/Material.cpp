#include "Material.h"
#include <format>
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* Material::GetComponentInspectionName() {
        return "Material";
    }

    void Material::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "MaterialGroupIndex", std::format("{}", MaterialGroupIndex) });
        OutFields.push_back(ComponentInspectionField{ "Flags", std::format("{}", Flags) });
    }
}
