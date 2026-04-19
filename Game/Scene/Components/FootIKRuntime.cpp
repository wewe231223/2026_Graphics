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
        OutFields.push_back(ComponentInspectionField{ "LeftToeEntityId", ToEntityIdText(mLeftToeEntityId) });
        OutFields.push_back(ComponentInspectionField{ "RightToeEntityId", ToEntityIdText(mRightToeEntityId) });
        OutFields.push_back(ComponentInspectionField{ "LeftShinEntityId", ToEntityIdText(mLeftShinEntityId) });
        OutFields.push_back(ComponentInspectionField{ "RightShinEntityId", ToEntityIdText(mRightShinEntityId) });
        OutFields.push_back(ComponentInspectionField{ "LeftThighEntityId", ToEntityIdText(mLeftThighEntityId) });
        OutFields.push_back(ComponentInspectionField{ "RightThighEntityId", ToEntityIdText(mRightThighEntityId) });
        OutFields.push_back(ComponentInspectionField{ "PelvisEntityId", ToEntityIdText(mPelvisEntityId) });
        OutFields.push_back(ComponentInspectionField{ "LeftCurrentOffset", ::std::to_string(mLeftCurrentOffset) });
        OutFields.push_back(ComponentInspectionField{ "RightCurrentOffset", ::std::to_string(mRightCurrentOffset) });
        OutFields.push_back(ComponentInspectionField{ "CurrentOffset", ::std::to_string(mCurrentOffset) });
        OutFields.push_back(ComponentInspectionField{ "LeftPlantWeight", ::std::to_string(mLeftPlantWeight) });
        OutFields.push_back(ComponentInspectionField{ "RightPlantWeight", ::std::to_string(mRightPlantWeight) });
        OutFields.push_back(ComponentInspectionField{ "Resolved", mResolved ? "true" : "false" });
    }
}
