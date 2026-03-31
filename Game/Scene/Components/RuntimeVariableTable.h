#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
#include "Game/Asset/AnimationGraphAsset.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    Component(RuntimeVariableTable)
        static constexpr std::uint32_t MaxParameterCount{ 64 };

        std::array<bool, MaxParameterCount> BoolValues{};
        std::array<std::int32_t, MaxParameterCount> IntValues{};
        std::array<float, MaxParameterCount> FloatValues{};
        std::array<bool, MaxParameterCount> TriggerConsumed{};

        bool TrySetBoolParameter(const std::vector<AnimationGraphAsset::AnimationGraphParameterDefinition>& ParameterDefinitions, std::string_view ParameterName, bool Value);
        bool TrySetIntParameter(const std::vector<AnimationGraphAsset::AnimationGraphParameterDefinition>& ParameterDefinitions, std::string_view ParameterName, std::int32_t Value);
        bool TrySetFloatParameter(const std::vector<AnimationGraphAsset::AnimationGraphParameterDefinition>& ParameterDefinitions, std::string_view ParameterName, float Value);
        bool TrySetTriggerParameter(const std::vector<AnimationGraphAsset::AnimationGraphParameterDefinition>& ParameterDefinitions, std::string_view ParameterName);
    EndComponent(RuntimeVariableTable)
}
