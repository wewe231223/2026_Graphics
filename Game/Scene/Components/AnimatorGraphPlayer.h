#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
#include "Game/Asset/AnimationGraphAsset.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    Component(AnimatorGraphPlayer)
        static constexpr std::uint32_t MaxParameterCount{ 64 };

        std::int32_t CurrentNodeIndex{ -1 };
        std::int32_t NextNodeIndex{ -1 };
        float CurrentLocalTime{};
        float CurrentNormalizedTime{};
        float BlendElapsed{};
        float BlendDuration{};
        bool IsInTransition{};
        bool PendingInterrupt{};

        std::int32_t SampleSourceClipIndex{ -1 };
        std::int32_t SampleDestinationClipIndex{ -1 };
        float SampleSourceLocalTime{};
        float SampleDestinationLocalTime{};
        float SampleBlendAlpha{};
        float SamplePlaySpeed{ 1.0f };
        bool SampleIsLoop{ true };

        std::array<bool, MaxParameterCount> BoolValues{};
        std::array<std::int32_t, MaxParameterCount> IntValues{};
        std::array<float, MaxParameterCount> FloatValues{};
        std::array<bool, MaxParameterCount> TriggerConsumed{};

        bool TrySetBoolParameter(const std::vector<AnimationGraphAsset::AnimationGraphParameterDefinition>& ParameterDefinitions, std::string_view ParameterName, bool Value);
        bool TrySetIntParameter(const std::vector<AnimationGraphAsset::AnimationGraphParameterDefinition>& ParameterDefinitions, std::string_view ParameterName, std::int32_t Value);
        bool TrySetFloatParameter(const std::vector<AnimationGraphAsset::AnimationGraphParameterDefinition>& ParameterDefinitions, std::string_view ParameterName, float Value);
        bool TrySetTriggerParameter(const std::vector<AnimationGraphAsset::AnimationGraphParameterDefinition>& ParameterDefinitions, std::string_view ParameterName);
    EndComponent(AnimatorGraphPlayer)
}
