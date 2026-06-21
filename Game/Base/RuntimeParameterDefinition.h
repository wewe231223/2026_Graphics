#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace Game {
    struct RuntimeParameterDefinition final {
        enum class ParameterType : std::uint8_t {
            Bool,
            Int,
            Float,
            Trigger,
        };

        std::string ParameterName{};
        ParameterType ParameterTypeValue{ ParameterType::Bool };
        std::variant<bool, std::int32_t, float> DefaultValue{ false };
    };
}
