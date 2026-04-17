#include "Game/Scene/Components/FootIKRuntime.h"

#include <format>
#include <string>
#include "Game/Scene/Components/ComponentInspection.h"

namespace {
    std::string ToEntityIdText(const Arche::EntityID EntityId) {
        if (EntityId == Arche::NullEntityID) {
            return "Null";
        }

        return ::std::format("index={}, generation={}, flags={}", EntityId.index, EntityId.generation, EntityId.flags);
    }
}

namespace Game {
    const char* FootIKRuntime::GetComponentInspectionName() {
        return "FootIKRuntime";
    }

    void FootIKRuntime::BuildComponentInspectionFields(::std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "LeftFootEntityId", ToEntityIdText(mLeftFootEntityId) });
        OutFields.push_back(ComponentInspectionField{ "RightFootEntityId", ToEntityIdText(mRightFootEntityId) });
        OutFields.push_back(ComponentInspectionField{ "LeftCurrentLift", ::std::to_string(mLeftCurrentLift) });
        OutFields.push_back(ComponentInspectionField{ "RightCurrentLift", ::std::to_string(mRightCurrentLift) });
        OutFields.push_back(ComponentInspectionField{ "CurrentLift", ::std::to_string(mCurrentLift) });
        OutFields.push_back(ComponentInspectionField{ "Resolved", mResolved ? "true" : "false" });
    }
}
