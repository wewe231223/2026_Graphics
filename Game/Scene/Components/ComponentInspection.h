#pragma once

#include <string>
#include <vector>
#include "Arche/Common.h"
#include "Arche/World.h"

namespace Game {
    struct ComponentInspectionField final {
        std::string Label{};
        std::string Value{};
    };

    struct ComponentInspectionSection final {
        std::string ComponentName{};
        std::vector<ComponentInspectionField> Fields{};
    };

    void BuildComponentInspectionSections(const Arche::World::WorldReadOnlyView& ReadOnlyWorld, Arche::EntityID EntityId, std::vector<ComponentInspectionSection>& OutSections);
}

