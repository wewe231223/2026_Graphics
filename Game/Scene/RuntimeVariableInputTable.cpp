#include "RuntimeVariableInputTable.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include <ryml.hpp>
#include <ryml_std.hpp>

#include "Game/Base/Input.h"
#include "Game/Scene/Components/RuntimeVariableTable.h"

namespace {
    using RuntimeVariableType = Game::RuntimeParameterDefinition::ParameterType;
    using InputEventType = Game::RuntimeVariableInputTable::InputEventType;

    std::string ToUpperText(const std::string& SourceText) {
        std::string UpperText{ SourceText };
        std::transform(UpperText.begin(), UpperText.end(), UpperText.begin(), [](const unsigned char Character) {
            return static_cast<char>(std::toupper(Character));
        });
        return UpperText;
    }

    bool TryParseRuntimeVariableType(const std::string& RuntimeVariableTypeText, RuntimeVariableType& OutRuntimeVariableType) {
        if (RuntimeVariableTypeText == "Bool") {
            OutRuntimeVariableType = RuntimeVariableType::Bool;
            return true;
        }

        if (RuntimeVariableTypeText == "Int") {
            OutRuntimeVariableType = RuntimeVariableType::Int;
            return true;
        }

        if (RuntimeVariableTypeText == "Float") {
            OutRuntimeVariableType = RuntimeVariableType::Float;
            return true;
        }

        if (RuntimeVariableTypeText == "Trigger") {
            OutRuntimeVariableType = RuntimeVariableType::Trigger;
            return true;
        }

        return false;
    }

    bool TryParseInputEventType(const std::string& InputEventTypeText, InputEventType& OutInputEventType) {
        if (InputEventTypeText == "Pressed") {
            OutInputEventType = InputEventType::Pressed;
            return true;
        }

        if (InputEventTypeText == "Down") {
            OutInputEventType = InputEventType::Down;
            return true;
        }

        if (InputEventTypeText == "Released") {
            OutInputEventType = InputEventType::Released;
            return true;
        }

        return false;
    }

    bool TryParseKeyboardKey(const std::string& KeyText, DirectX::Keyboard::Keys& OutKey) {
        static const std::unordered_map<std::string_view, DirectX::Keyboard::Keys> KeyMap{
            { "SPACE", DirectX::Keyboard::Keys::Space },
            { "ENTER", DirectX::Keyboard::Keys::Enter },
            { "TAB", DirectX::Keyboard::Keys::Tab },
            { "ESC", DirectX::Keyboard::Keys::Escape },
            { "ESCAPE", DirectX::Keyboard::Keys::Escape },
            { "LSHIFT", DirectX::Keyboard::Keys::LeftShift },
            { "RSHIFT", DirectX::Keyboard::Keys::RightShift },
            { "LCTRL", DirectX::Keyboard::Keys::LeftControl },
            { "RCTRL", DirectX::Keyboard::Keys::RightControl },
            { "LALT", DirectX::Keyboard::Keys::LeftAlt },
            { "RALT", DirectX::Keyboard::Keys::RightAlt },
            { "UP", DirectX::Keyboard::Keys::Up },
            { "DOWN", DirectX::Keyboard::Keys::Down },
            { "LEFT", DirectX::Keyboard::Keys::Left },
            { "RIGHT", DirectX::Keyboard::Keys::Right },
            { "F1", DirectX::Keyboard::Keys::F1 },
            { "F2", DirectX::Keyboard::Keys::F2 },
            { "F3", DirectX::Keyboard::Keys::F3 },
            { "F4", DirectX::Keyboard::Keys::F4 },
            { "F5", DirectX::Keyboard::Keys::F5 },
            { "F6", DirectX::Keyboard::Keys::F6 },
            { "F7", DirectX::Keyboard::Keys::F7 },
            { "F8", DirectX::Keyboard::Keys::F8 },
            { "F9", DirectX::Keyboard::Keys::F9 },
            { "F10", DirectX::Keyboard::Keys::F10 },
            { "F11", DirectX::Keyboard::Keys::F11 },
            { "F12", DirectX::Keyboard::Keys::F12 },
        };

        const std::string UpperKeyText{ ToUpperText(KeyText) };
        if (UpperKeyText.size() == 1) {
            const char Character{ UpperKeyText[0] };
            if (Character >= 'A' && Character <= 'Z') {
                OutKey = static_cast<DirectX::Keyboard::Keys>(DirectX::Keyboard::Keys::A + (Character - 'A'));
                return true;
            }

            if (Character >= '0' && Character <= '9') {
                OutKey = static_cast<DirectX::Keyboard::Keys>(DirectX::Keyboard::Keys::D0 + (Character - '0'));
                return true;
            }
        }

        const std::unordered_map<std::string_view, DirectX::Keyboard::Keys>::const_iterator KeyIter{ KeyMap.find(UpperKeyText) };
        if (KeyIter == KeyMap.end()) {
            return false;
        }

        OutKey = KeyIter->second;
        return true;
    }

    bool IsInputEventTriggered(const Globals::Input& InputInstance, DirectX::Keyboard::Keys Key, InputEventType InputEventTypeValue) {
        if (InputEventTypeValue == InputEventType::Pressed) {
            return InputInstance.IsKeyPressed(Key);
        }

        if (InputEventTypeValue == InputEventType::Down) {
            return InputInstance.IsKeyDown(Key);
        }

        return InputInstance.IsKeyReleased(Key);
    }
}

namespace Game {
    RuntimeVariableInputTable::RuntimeVariableInputTable() {
    }

    RuntimeVariableInputTable::~RuntimeVariableInputTable() {
    }

    RuntimeVariableInputTable::RuntimeVariableInputTable(const RuntimeVariableInputTable& Other)
        : mRuntimeVariableDefinitions{ Other.mRuntimeVariableDefinitions },
        mInputBindings{ Other.mInputBindings } {
    }

    RuntimeVariableInputTable& RuntimeVariableInputTable::operator=(const RuntimeVariableInputTable& Other) {
        if (this == &Other) {
            return *this;
        }

        mRuntimeVariableDefinitions = Other.mRuntimeVariableDefinitions;
        mInputBindings = Other.mInputBindings;
        return *this;
    }

    RuntimeVariableInputTable::RuntimeVariableInputTable(RuntimeVariableInputTable&& Other) noexcept
        : mRuntimeVariableDefinitions{ std::move(Other.mRuntimeVariableDefinitions) },
        mInputBindings{ std::move(Other.mInputBindings) } {
    }

    RuntimeVariableInputTable& RuntimeVariableInputTable::operator=(RuntimeVariableInputTable&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mRuntimeVariableDefinitions = std::move(Other.mRuntimeVariableDefinitions);
        mInputBindings = std::move(Other.mInputBindings);
        return *this;
    }

    void RuntimeVariableInputTable::Clear() {
        mRuntimeVariableDefinitions.clear();
        mInputBindings.clear();
    }

    bool RuntimeVariableInputTable::LoadFromFile(const std::string& YamlFilePath, std::vector<std::string>& OutErrors) {
        Clear();

        std::ifstream InputStream{ YamlFilePath, std::ios::in | std::ios::binary };
        if (InputStream.is_open() == false) {
            OutErrors.push_back(std::string{ "입력 테이블 YAML 파일을 열 수 없습니다: " } + YamlFilePath);
            return false;
        }

        std::stringstream Buffer{};
        Buffer << InputStream.rdbuf();
        const std::string YamlText{ Buffer.str() };

        c4::yml::Tree YamlTree{};
        c4::yml::parse_in_arena(c4::to_csubstr(YamlText), &YamlTree);
        const c4::yml::ConstNodeRef RootNode{ YamlTree.rootref() };

        if (RootNode.has_child("Variables")) {
            const c4::yml::ConstNodeRef RuntimeVariablesNode{ RootNode["Variables"] };
            for (const c4::yml::ConstNodeRef RuntimeVariableNode : RuntimeVariablesNode.children()) {
                RuntimeParameterDefinition NewRuntimeVariableDefinition{};
                std::string RuntimeVariableTypeText{};
                if (RuntimeVariableNode.has_child("Name")) {
                    RuntimeVariableNode["Name"] >> NewRuntimeVariableDefinition.ParameterName;
                }

                if (RuntimeVariableNode.has_child("Type")) {
                    RuntimeVariableNode["Type"] >> RuntimeVariableTypeText;
                }

                if (TryParseRuntimeVariableType(RuntimeVariableTypeText, NewRuntimeVariableDefinition.ParameterTypeValue) == false) {
                    OutErrors.push_back(std::string{ "입력 테이블 Variables Type 값 오류: " } + RuntimeVariableTypeText);
                    continue;
                }

                if (NewRuntimeVariableDefinition.ParameterTypeValue == RuntimeVariableType::Bool) {
                    bool BoolInitialValue{};
                    if (RuntimeVariableNode.has_child("InitialValue")) {
                        RuntimeVariableNode["InitialValue"] >> BoolInitialValue;
                    }
                    NewRuntimeVariableDefinition.DefaultValue = BoolInitialValue;
                }
                else if (NewRuntimeVariableDefinition.ParameterTypeValue == RuntimeVariableType::Int) {
                    std::int32_t IntInitialValue{};
                    if (RuntimeVariableNode.has_child("InitialValue")) {
                        RuntimeVariableNode["InitialValue"] >> IntInitialValue;
                    }
                    NewRuntimeVariableDefinition.DefaultValue = IntInitialValue;
                }
                else if (NewRuntimeVariableDefinition.ParameterTypeValue == RuntimeVariableType::Float) {
                    float FloatInitialValue{};
                    if (RuntimeVariableNode.has_child("InitialValue")) {
                        RuntimeVariableNode["InitialValue"] >> FloatInitialValue;
                    }
                    NewRuntimeVariableDefinition.DefaultValue = FloatInitialValue;
                }
                else {
                    bool TriggerInitialValue{};
                    if (RuntimeVariableNode.has_child("InitialValue")) {
                        RuntimeVariableNode["InitialValue"] >> TriggerInitialValue;
                    }
                    NewRuntimeVariableDefinition.DefaultValue = TriggerInitialValue;
                }

                mRuntimeVariableDefinitions.push_back(std::move(NewRuntimeVariableDefinition));
            }
        }

        if (RootNode.has_child("InputBindings")) {
            const c4::yml::ConstNodeRef InputBindingsNode{ RootNode["InputBindings"] };
            for (const c4::yml::ConstNodeRef InputBindingNode : InputBindingsNode.children()) {
                InputBinding NewInputBinding{};
                std::string KeyText{};
                std::string InputEventTypeText{};
                if (InputBindingNode.has_child("Key")) {
                    InputBindingNode["Key"] >> KeyText;
                }

                if (InputBindingNode.has_child("Event")) {
                    InputBindingNode["Event"] >> InputEventTypeText;
                }

                if (TryParseKeyboardKey(KeyText, NewInputBinding.Key) == false) {
                    OutErrors.push_back(std::string{ "입력 테이블 Key 값 오류: " } + KeyText);
                    continue;
                }

                if (TryParseInputEventType(InputEventTypeText, NewInputBinding.InputEventTypeValue) == false) {
                    OutErrors.push_back(std::string{ "입력 테이블 Event 값 오류: " } + InputEventTypeText);
                    continue;
                }

                if (InputBindingNode.has_child("Changes") == false) {
                    OutErrors.push_back("입력 테이블 Changes 누락");
                    continue;
                }

                const c4::yml::ConstNodeRef ChangesNode{ InputBindingNode["Changes"] };
                for (const c4::yml::ConstNodeRef ChangeNode : ChangesNode.children()) {
                    RuntimeVariableChange NewRuntimeVariableChange{};
                    std::string RuntimeVariableTypeText{};
                    if (ChangeNode.has_child("Name")) {
                        ChangeNode["Name"] >> NewRuntimeVariableChange.RuntimeVariableName;
                    }

                    if (ChangeNode.has_child("Type")) {
                        ChangeNode["Type"] >> RuntimeVariableTypeText;
                    }

                    if (TryParseRuntimeVariableType(RuntimeVariableTypeText, NewRuntimeVariableChange.RuntimeVariableTypeValue) == false) {
                        OutErrors.push_back(std::string{ "입력 테이블 Changes Type 값 오류: " } + RuntimeVariableTypeText);
                        continue;
                    }

                    if (NewRuntimeVariableChange.RuntimeVariableTypeValue == RuntimeVariableType::Bool) {
                        if (ChangeNode.has_child("Value")) {
                            ChangeNode["Value"] >> NewRuntimeVariableChange.BoolValue;
                        }
                    }
                    else if (NewRuntimeVariableChange.RuntimeVariableTypeValue == RuntimeVariableType::Int) {
                        if (ChangeNode.has_child("Value")) {
                            ChangeNode["Value"] >> NewRuntimeVariableChange.IntValue;
                        }
                    }
                    else if (NewRuntimeVariableChange.RuntimeVariableTypeValue == RuntimeVariableType::Float) {
                        if (ChangeNode.has_child("Value")) {
                            ChangeNode["Value"] >> NewRuntimeVariableChange.FloatValue;
                        }
                    }

                    NewInputBinding.RuntimeVariableChanges.push_back(std::move(NewRuntimeVariableChange));
                }

                mInputBindings.push_back(std::move(NewInputBinding));
            }
        }

        return OutErrors.empty();
    }

    void RuntimeVariableInputTable::ApplyInitialValues(RuntimeVariableTable& OutVariableTable, const std::vector<RuntimeParameterDefinition>& ParameterDefinitions) const {
        for (const RuntimeParameterDefinition& RuntimeVariableDefinitionValue : mRuntimeVariableDefinitions) {
            if (RuntimeVariableDefinitionValue.ParameterTypeValue == RuntimeVariableType::Bool) {
                OutVariableTable.TrySetBoolParameter(ParameterDefinitions, RuntimeVariableDefinitionValue.ParameterName, std::get<bool>(RuntimeVariableDefinitionValue.DefaultValue));
            }
            else if (RuntimeVariableDefinitionValue.ParameterTypeValue == RuntimeVariableType::Int) {
                OutVariableTable.TrySetIntParameter(ParameterDefinitions, RuntimeVariableDefinitionValue.ParameterName, std::get<std::int32_t>(RuntimeVariableDefinitionValue.DefaultValue));
            }
            else if (RuntimeVariableDefinitionValue.ParameterTypeValue == RuntimeVariableType::Float) {
                OutVariableTable.TrySetFloatParameter(ParameterDefinitions, RuntimeVariableDefinitionValue.ParameterName, std::get<float>(RuntimeVariableDefinitionValue.DefaultValue));
            }
            else {
                OutVariableTable.TrySetTriggerParameter(ParameterDefinitions, RuntimeVariableDefinitionValue.ParameterName);
            }
        }
    }

    void RuntimeVariableInputTable::ApplyInputBindings(RuntimeVariableTable& OutVariableTable, const std::vector<RuntimeParameterDefinition>& ParameterDefinitions) const {
        const Globals::Input& InputInstance{ Globals::Input::Get() };
        for (const InputBinding& InputBindingValue : mInputBindings) {
            if (IsInputEventTriggered(InputInstance, InputBindingValue.Key, InputBindingValue.InputEventTypeValue) == false) {
                continue;
            }

            for (const RuntimeVariableChange& RuntimeVariableChangeValue : InputBindingValue.RuntimeVariableChanges) {
                if (RuntimeVariableChangeValue.RuntimeVariableTypeValue == RuntimeVariableType::Bool) {
                    OutVariableTable.TrySetBoolParameter(ParameterDefinitions, RuntimeVariableChangeValue.RuntimeVariableName, RuntimeVariableChangeValue.BoolValue);
                }
                else if (RuntimeVariableChangeValue.RuntimeVariableTypeValue == RuntimeVariableType::Int) {
                    OutVariableTable.TrySetIntParameter(ParameterDefinitions, RuntimeVariableChangeValue.RuntimeVariableName, RuntimeVariableChangeValue.IntValue);
                }
                else if (RuntimeVariableChangeValue.RuntimeVariableTypeValue == RuntimeVariableType::Float) {
                    OutVariableTable.TrySetFloatParameter(ParameterDefinitions, RuntimeVariableChangeValue.RuntimeVariableName, RuntimeVariableChangeValue.FloatValue);
                }
                else {
                    OutVariableTable.TrySetTriggerParameter(ParameterDefinitions, RuntimeVariableChangeValue.RuntimeVariableName);
                }
            }
        }
    }
}
