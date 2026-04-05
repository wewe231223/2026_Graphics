#include "RuntimeVariableTable.h"
#include <optional>
#include "Game/Scene/Components/ComponentInspection.h"


namespace {
    std::optional<std::size_t> TryFindParameterIndex(const std::vector<Game::RuntimeParameterDefinition>& ParameterDefinitions, const std::string_view ParameterName) {
        for (std::size_t ParameterIndex{ 0 }; ParameterIndex < ParameterDefinitions.size(); ++ParameterIndex) {
            if (ParameterDefinitions[ParameterIndex].ParameterName == ParameterName) {
                return ParameterIndex;
            }
        }

        return std::nullopt;
    }
}

namespace Game {
    const char* RuntimeVariableTable::GetComponentInspectionName() {
        return "RuntimeVariableTable";
    }

    void RuntimeVariableTable::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.clear();
        OutFields.push_back(ComponentInspectionField{ "Summary", "세부 값은 RuntimeVariableTable 패널에서 확인" });
    }

    bool RuntimeVariableTable::TrySetBoolParameter(const std::vector<RuntimeParameterDefinition>& ParameterDefinitions, const std::string_view ParameterName, const bool Value) {
        const std::optional<std::size_t> ParameterIndex{ TryFindParameterIndex(ParameterDefinitions, ParameterName) };
        if (ParameterIndex.has_value() == false || ParameterIndex.value() >= RuntimeVariableTableMaxParameterCount) {
            return false;
        }

        const RuntimeParameterDefinition& Definition{ ParameterDefinitions[ParameterIndex.value()] };
        if (Definition.ParameterTypeValue != RuntimeParameterDefinition::ParameterType::Bool) {
            return false;
        }

        BoolValues[ParameterIndex.value()] = Value;
        return true;
    }

    bool RuntimeVariableTable::TrySetIntParameter(const std::vector<RuntimeParameterDefinition>& ParameterDefinitions, const std::string_view ParameterName, const std::int32_t Value) {
        const std::optional<std::size_t> ParameterIndex{ TryFindParameterIndex(ParameterDefinitions, ParameterName) };
        if (ParameterIndex.has_value() == false || ParameterIndex.value() >= RuntimeVariableTableMaxParameterCount) {
            return false;
        }

        const RuntimeParameterDefinition& Definition{ ParameterDefinitions[ParameterIndex.value()] };
        if (Definition.ParameterTypeValue != RuntimeParameterDefinition::ParameterType::Int) {
            return false;
        }

        IntValues[ParameterIndex.value()] = Value;
        return true;
    }

    bool RuntimeVariableTable::TrySetFloatParameter(const std::vector<RuntimeParameterDefinition>& ParameterDefinitions, const std::string_view ParameterName, const float Value) {
        const std::optional<std::size_t> ParameterIndex{ TryFindParameterIndex(ParameterDefinitions, ParameterName) };
        if (ParameterIndex.has_value() == false || ParameterIndex.value() >= RuntimeVariableTableMaxParameterCount) {
            return false;
        }

        const RuntimeParameterDefinition& Definition{ ParameterDefinitions[ParameterIndex.value()] };
        if (Definition.ParameterTypeValue != RuntimeParameterDefinition::ParameterType::Float) {
            return false;
        }

        FloatValues[ParameterIndex.value()] = Value;
        return true;
    }

    bool RuntimeVariableTable::TrySetTriggerParameter(const std::vector<RuntimeParameterDefinition>& ParameterDefinitions, const std::string_view ParameterName) {
        const std::optional<std::size_t> ParameterIndex{ TryFindParameterIndex(ParameterDefinitions, ParameterName) };
        if (ParameterIndex.has_value() == false || ParameterIndex.value() >= RuntimeVariableTableMaxParameterCount) {
            return false;
        }

        const RuntimeParameterDefinition& Definition{ ParameterDefinitions[ParameterIndex.value()] };
        if (Definition.ParameterTypeValue != RuntimeParameterDefinition::ParameterType::Trigger) {
            return false;
        }

        BoolValues[ParameterIndex.value()] = true;
        TriggerConsumed[ParameterIndex.value()] = false;
        return true;
    }
}
