#include "AnimatorGraphPlayer.h"

#include <format>
#include <optional>
#include "Game/Scene/Components/ComponentInspection.h"

namespace {
    std::optional<std::size_t> TryFindParameterIndex(const std::vector<Game::AnimationGraphAsset::AnimationGraphParameterDefinition>& ParameterDefinitions, const std::string_view ParameterName) {
        for (std::size_t ParameterIndex{ 0 }; ParameterIndex < ParameterDefinitions.size(); ++ParameterIndex) {
            if (ParameterDefinitions[ParameterIndex].ParameterName == ParameterName) {
                return ParameterIndex;
            }
        }

        return std::nullopt;
    }
}

namespace Game {
    const char* AnimatorGraphPlayer::GetComponentInspectionName() {
        return "AnimatorGraphPlayer";
    }

    void AnimatorGraphPlayer::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "CurrentNodeIndex", std::format("{}", CurrentNodeIndex) });
        OutFields.push_back(ComponentInspectionField{ "NextNodeIndex", std::format("{}", NextNodeIndex) });
        OutFields.push_back(ComponentInspectionField{ "CurrentLocalTime", std::format("{}", CurrentLocalTime) });
        OutFields.push_back(ComponentInspectionField{ "BlendAlpha", std::format("{}", SampleBlendAlpha) });
        OutFields.push_back(ComponentInspectionField{ "IsInTransition", IsInTransition ? "true" : "false" });
    }

    bool AnimatorGraphPlayer::TrySetBoolParameter(const std::vector<AnimationGraphAsset::AnimationGraphParameterDefinition>& ParameterDefinitions, const std::string_view ParameterName, const bool Value) {
        const std::optional<std::size_t> ParameterIndex{ TryFindParameterIndex(ParameterDefinitions, ParameterName) };
        if (ParameterIndex.has_value() == false || ParameterIndex.value() >= MaxParameterCount) {
            return false;
        }

        const AnimationGraphAsset::AnimationGraphParameterDefinition& Definition{ ParameterDefinitions[ParameterIndex.value()] };
        if (Definition.ParameterTypeValue != AnimationGraphAsset::ParameterType::Bool) {
            return false;
        }

        BoolValues[ParameterIndex.value()] = Value;
        return true;
    }

    bool AnimatorGraphPlayer::TrySetIntParameter(const std::vector<AnimationGraphAsset::AnimationGraphParameterDefinition>& ParameterDefinitions, const std::string_view ParameterName, const std::int32_t Value) {
        const std::optional<std::size_t> ParameterIndex{ TryFindParameterIndex(ParameterDefinitions, ParameterName) };
        if (ParameterIndex.has_value() == false || ParameterIndex.value() >= MaxParameterCount) {
            return false;
        }

        const AnimationGraphAsset::AnimationGraphParameterDefinition& Definition{ ParameterDefinitions[ParameterIndex.value()] };
        if (Definition.ParameterTypeValue != AnimationGraphAsset::ParameterType::Int) {
            return false;
        }

        IntValues[ParameterIndex.value()] = Value;
        return true;
    }

    bool AnimatorGraphPlayer::TrySetFloatParameter(const std::vector<AnimationGraphAsset::AnimationGraphParameterDefinition>& ParameterDefinitions, const std::string_view ParameterName, const float Value) {
        const std::optional<std::size_t> ParameterIndex{ TryFindParameterIndex(ParameterDefinitions, ParameterName) };
        if (ParameterIndex.has_value() == false || ParameterIndex.value() >= MaxParameterCount) {
            return false;
        }

        const AnimationGraphAsset::AnimationGraphParameterDefinition& Definition{ ParameterDefinitions[ParameterIndex.value()] };
        if (Definition.ParameterTypeValue != AnimationGraphAsset::ParameterType::Float) {
            return false;
        }

        FloatValues[ParameterIndex.value()] = Value;
        return true;
    }

    bool AnimatorGraphPlayer::TrySetTriggerParameter(const std::vector<AnimationGraphAsset::AnimationGraphParameterDefinition>& ParameterDefinitions, const std::string_view ParameterName) {
        const std::optional<std::size_t> ParameterIndex{ TryFindParameterIndex(ParameterDefinitions, ParameterName) };
        if (ParameterIndex.has_value() == false || ParameterIndex.value() >= MaxParameterCount) {
            return false;
        }

        const AnimationGraphAsset::AnimationGraphParameterDefinition& Definition{ ParameterDefinitions[ParameterIndex.value()] };
        if (Definition.ParameterTypeValue != AnimationGraphAsset::ParameterType::Trigger) {
            return false;
        }

        BoolValues[ParameterIndex.value()] = true;
        TriggerConsumed[ParameterIndex.value()] = false;
        return true;
    }
}
