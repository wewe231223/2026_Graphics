#include "AnimationGraphAsset.h"

#include <algorithm>

namespace Game {
    AnimationGraphAsset::AnimationGraphAsset() = default;
    AnimationGraphAsset::~AnimationGraphAsset() = default;
    AnimationGraphAsset::AnimationGraphAsset(const AnimationGraphAsset& Other) = default;
    AnimationGraphAsset& AnimationGraphAsset::operator=(const AnimationGraphAsset& Other) = default;
    AnimationGraphAsset::AnimationGraphAsset(AnimationGraphAsset&& Other) noexcept = default;
    AnimationGraphAsset& AnimationGraphAsset::operator=(AnimationGraphAsset&& Other) noexcept = default;

    const std::string& AnimationGraphAsset::GetGraphName() const {
        return mGraphName;
    }

    const std::vector<AnimationGraphAsset::AnimationGraphNodeAsset>& AnimationGraphAsset::GetNodes() const {
        return mNodes;
    }

    const std::vector<AnimationGraphAsset::AnimationGraphTransitionAsset>& AnimationGraphAsset::GetTransitions() const {
        return mTransitions;
    }

    const std::vector<RuntimeParameterDefinition>& AnimationGraphAsset::GetParameterDefinitions() const {
        return mParameterDefinitions;
    }

    std::int32_t AnimationGraphAsset::GetDefaultNodeIndex() const {
        return mDefaultNodeIndex;
    }

    bool AnimationGraphAsset::TryGetNodeIndexByName(const std::string& NodeName, std::uint32_t& OutNodeIndex) const {
        const std::unordered_map<std::string, std::uint32_t>::const_iterator FoundIter{ mNodeNameToIndex.find(NodeName) };
        if (FoundIter == mNodeNameToIndex.end()) {
            return false;
        }

        OutNodeIndex = FoundIter->second;
        return true;
    }

    void AnimationGraphAsset::SetGraphName(const std::string& GraphName) {
        mGraphName = GraphName;
    }

    void AnimationGraphAsset::SetNodes(std::vector<AnimationGraphNodeAsset> Nodes) {
        mNodes = std::move(Nodes);
        RebuildNodeNameLookup();
    }

    void AnimationGraphAsset::SetTransitions(std::vector<AnimationGraphTransitionAsset> Transitions) {
        mTransitions = std::move(Transitions);
    }

    void AnimationGraphAsset::SetParameterDefinitions(std::vector<RuntimeParameterDefinition> ParameterDefinitions) {
        mParameterDefinitions = std::move(ParameterDefinitions);
    }

    bool AnimationGraphAsset::Validate(std::string& OutErrorText) {
        mDefaultNodeIndex = -1;
        RebuildNodeNameLookup();

        std::int32_t DefaultNodeCount{ 0 };
        for (std::size_t NodeIndex{ 0 }; NodeIndex < mNodes.size(); ++NodeIndex) {
            if (mNodes[NodeIndex].IsDefault == false) {
                continue;
            }

            DefaultNodeCount += 1;
            mDefaultNodeIndex = static_cast<std::int32_t>(NodeIndex);
        }

        if (DefaultNodeCount != 1) {
            OutErrorText = "AnimationGraph default node count must be exactly 1.";
            return false;
        }

        for (const AnimationGraphTransitionAsset& Transition : mTransitions) {
            if (Transition.FromNodeIndex >= mNodes.size() || Transition.ToNodeIndex >= mNodes.size()) {
                OutErrorText = "AnimationGraph transition node index is out of range.";
                return false;
            }
        }

        std::stable_sort(mTransitions.begin(), mTransitions.end(), [](const AnimationGraphTransitionAsset& Left, const AnimationGraphTransitionAsset& Right) {
            if (Left.Priority != Right.Priority) {
                return Left.Priority < Right.Priority;
            }

            return Left.FileOrder < Right.FileOrder;
        });

        return true;
    }

    void AnimationGraphAsset::RebuildNodeNameLookup() {
        mNodeNameToIndex.clear();
        for (std::uint32_t NodeIndex{ 0 }; NodeIndex < static_cast<std::uint32_t>(mNodes.size()); ++NodeIndex) {
            mNodeNameToIndex.insert_or_assign(mNodes[NodeIndex].NodeName, NodeIndex);
        }
    }
}
