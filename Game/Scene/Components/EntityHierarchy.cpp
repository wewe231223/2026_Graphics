#include "EntityHierarchy.h"
#include <format>
#include "Game/Scene/Components/ComponentInspection.h"

namespace {
    std::string ToEntityIdText(Arche::EntityID EntityId) {
        if (EntityId == Arche::NullEntityID) {
            return "Null";
        }

        return std::format("index={}, generation={}, flags={}", EntityId.index, EntityId.generation, EntityId.flags);
    }
}

namespace Game {
    const char* EntityHierarchy::GetComponentInspectionName() {
        return "EntityHierarchy";
    }

    void EntityHierarchy::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "Self", ToEntityIdText(self) });
        OutFields.push_back(ComponentInspectionField{ "Parent", ToEntityIdText(parent) });
        OutFields.push_back(ComponentInspectionField{ "FirstChild", ToEntityIdText(firstChild) });
        OutFields.push_back(ComponentInspectionField{ "NextSibling", ToEntityIdText(nextSibling) });
    }
}
