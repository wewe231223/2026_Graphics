#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "Game/Base/RuntimeParameterDefinition.h"

namespace Game {
    class AnimationGraphAsset final {
    public:
        enum class ConditionOperator : std::uint8_t {
            Equals,
            NotEquals,
            Greater,
            GreaterOrEqual,
            Less,
            LessOrEqual,
        };

        struct AnimationGraphNodeAsset final {
            std::string NodeName{};
            std::string AnimationFile{};
            std::int32_t ClipIndex{};
            bool IsLoop{};
            float PlaySpeed{ 1.0f };
            bool IsDefault{};
        };

        struct AnimationGraphConditionAsset final {
            std::uint32_t ParameterIndex{};
            ConditionOperator Operator{ ConditionOperator::Equals };
            bool BoolValue{};
            std::int32_t IntValue{};
            float FloatValue{};
        };

        struct AnimationGraphTransitionAsset final {
            std::uint32_t FromNodeIndex{};
            std::uint32_t ToNodeIndex{};
            std::vector<AnimationGraphConditionAsset> Conditions{};
            bool HasExitTime{};
            float ExitTimeNormalized{};
            bool CanInterrupt{};
            std::int32_t Priority{};
            float BlendDuration{ 0.1f };
            std::uint32_t FileOrder{};
        };

    public:
        AnimationGraphAsset();
        ~AnimationGraphAsset();
        AnimationGraphAsset(const AnimationGraphAsset& Other);
        AnimationGraphAsset& operator=(const AnimationGraphAsset& Other);
        AnimationGraphAsset(AnimationGraphAsset&& Other) noexcept;
        AnimationGraphAsset& operator=(AnimationGraphAsset&& Other) noexcept;

    public:
        const std::string& GetGraphName() const;
        const std::vector<AnimationGraphNodeAsset>& GetNodes() const;
        const std::vector<AnimationGraphTransitionAsset>& GetTransitions() const;
        const std::vector<RuntimeParameterDefinition>& GetParameterDefinitions() const;
        std::int32_t GetDefaultNodeIndex() const;
        bool TryGetNodeIndexByName(const std::string& NodeName, std::uint32_t& OutNodeIndex) const;

        void SetGraphName(const std::string& GraphName);
        void SetNodes(std::vector<AnimationGraphNodeAsset> Nodes);
        void SetTransitions(std::vector<AnimationGraphTransitionAsset> Transitions);
        void SetParameterDefinitions(std::vector<RuntimeParameterDefinition> ParameterDefinitions);

        bool Validate(std::string& OutErrorText);

    private:
        void RebuildNodeNameLookup();

    private:
        std::string mGraphName{};
        std::vector<AnimationGraphNodeAsset> mNodes{};
        std::vector<AnimationGraphTransitionAsset> mTransitions{};
        std::vector<RuntimeParameterDefinition> mParameterDefinitions{};
        std::int32_t mDefaultNodeIndex{ -1 };
        std::unordered_map<std::string, std::uint32_t> mNodeNameToIndex{};
    };
}
