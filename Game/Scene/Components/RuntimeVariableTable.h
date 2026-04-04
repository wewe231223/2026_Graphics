#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
#include "Game/Base/RuntimeParameterDefinition.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    inline constexpr std::uint32_t RuntimeVariableTableMaxParameterCount{ 64 };

    Component(RuntimeVariableTable)
        std::array<bool, RuntimeVariableTableMaxParameterCount> BoolValues{};
        std::array<std::int32_t, RuntimeVariableTableMaxParameterCount> IntValues{};
        std::array<float, RuntimeVariableTableMaxParameterCount> FloatValues{};
        std::array<bool, RuntimeVariableTableMaxParameterCount> TriggerConsumed{};

        bool TrySetBoolParameter(const std::vector<RuntimeParameterDefinition>& ParameterDefinitions, std::string_view ParameterName, bool Value);
        bool TrySetIntParameter(const std::vector<RuntimeParameterDefinition>& ParameterDefinitions, std::string_view ParameterName, std::int32_t Value);
        bool TrySetFloatParameter(const std::vector<RuntimeParameterDefinition>& ParameterDefinitions, std::string_view ParameterName, float Value);
        bool TrySetTriggerParameter(const std::vector<RuntimeParameterDefinition>& ParameterDefinitions, std::string_view ParameterName);
    EndComponent(RuntimeVariableTable)
}
