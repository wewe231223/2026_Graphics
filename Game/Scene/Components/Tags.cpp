#include "Tags.h"
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* LocalPlayerTag::GetComponentInspectionName() {
        return "LocalPlayerTag";
    }

    void LocalPlayerTag::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "Present", "true" });
    }
}
