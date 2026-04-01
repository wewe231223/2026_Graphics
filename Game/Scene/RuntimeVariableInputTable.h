#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "External/Include/DirectXTK12/Keyboard.h"
#include "Game/Base/RuntimeParameterDefinition.h"

namespace Game {
    struct RuntimeVariableTable;
}

namespace Game {
    class RuntimeVariableInputTable final {
    public:
        enum class InputEventType : std::uint8_t {
            Pressed,
            Down,
            Released,
        };

        struct RuntimeVariableChange final {
            std::string RuntimeVariableName{};
            RuntimeParameterDefinition::ParameterType RuntimeVariableTypeValue{ RuntimeParameterDefinition::ParameterType::Bool };
            bool BoolValue{};
            std::int32_t IntValue{};
            float FloatValue{};
        };

        struct InputBinding final {
            DirectX::Keyboard::Keys Key{ DirectX::Keyboard::Keys::None };
            InputEventType InputEventTypeValue{ InputEventType::Pressed };
            std::vector<RuntimeVariableChange> RuntimeVariableChanges{};
        };

    public:
        RuntimeVariableInputTable();
        ~RuntimeVariableInputTable();
        RuntimeVariableInputTable(const RuntimeVariableInputTable& Other);
        RuntimeVariableInputTable& operator=(const RuntimeVariableInputTable& Other);
        RuntimeVariableInputTable(RuntimeVariableInputTable&& Other) noexcept;
        RuntimeVariableInputTable& operator=(RuntimeVariableInputTable&& Other) noexcept;

    public:
        void Clear();
        bool LoadFromFile(const std::string& YamlFilePath, std::vector<std::string>& OutErrors);

        void ApplyInitialValues(RuntimeVariableTable& OutVariableTable, const std::vector<RuntimeParameterDefinition>& ParameterDefinitions) const;
        void ApplyInputBindings(RuntimeVariableTable& OutVariableTable, const std::vector<RuntimeParameterDefinition>& ParameterDefinitions) const;

    private:
        std::vector<RuntimeParameterDefinition> mRuntimeVariableDefinitions{};
        std::vector<InputBinding> mInputBindings{};
    };
}
