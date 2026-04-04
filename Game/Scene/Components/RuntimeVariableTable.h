#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
#include "Game/Base/RuntimeParameterDefinition.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    inline constexpr ::std::uint32_t RuntimeVariableTableMaxParameterCount{ 64 };
    using RuntimeVariableBoolArray = ::std::array<bool, RuntimeVariableTableMaxParameterCount>;
    using RuntimeVariableIntArray = ::std::array<::std::int32_t, RuntimeVariableTableMaxParameterCount>;
    using RuntimeVariableFloatArray = ::std::array<float, RuntimeVariableTableMaxParameterCount>;

    ComponentDecl(
        RuntimeVariableTable,
        ComponentFields(
            ComponentField(RuntimeVariableBoolArray, BoolValues, {})
            ComponentField(RuntimeVariableIntArray, IntValues, {})
            ComponentField(RuntimeVariableFloatArray, FloatValues, {})
            ComponentField(RuntimeVariableBoolArray, TriggerConsumed, {})
        ),
        ComponentMethods(
            ComponentMethod(bool TrySetBoolParameter(const ::std::vector<RuntimeParameterDefinition>& ParameterDefinitions, ::std::string_view ParameterName, bool Value), TrySetBoolParameter)
            ComponentMethod(bool TrySetIntParameter(const ::std::vector<RuntimeParameterDefinition>& ParameterDefinitions, ::std::string_view ParameterName, ::std::int32_t Value), TrySetIntParameter)
            ComponentMethod(bool TrySetFloatParameter(const ::std::vector<RuntimeParameterDefinition>& ParameterDefinitions, ::std::string_view ParameterName, float Value), TrySetFloatParameter)
            ComponentMethod(bool TrySetTriggerParameter(const ::std::vector<RuntimeParameterDefinition>& ParameterDefinitions, ::std::string_view ParameterName), TrySetTriggerParameter)
        )
    );
}
