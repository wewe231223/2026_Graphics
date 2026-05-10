#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
#include "Game/Scene/System.h"

namespace asset {
    struct AnimationChannel;
    struct AnimationClip;
}

namespace Game {
    struct Bone;
    struct Transform;
    class Model;
    class ModelNode;

    class AnimateSystem final : public ISystem {
    private:
        struct AnimationChannelLookupCacheEntry final {
            const Model* mModel{};
            const asset::AnimationClip* mClip{};
            const ModelNode* mNodeData{};
            std::size_t mNodeCount{};
            const asset::AnimationChannel* mChannelData{};
            std::size_t mChannelCount{};
            std::vector<const asset::AnimationChannel*> mLookup{};
        };

        struct NodeToParentInverseCacheEntry final {
            const Model* mModel{};
            const ModelNode* mNode{};
            std::uint32_t mNodeIndex{};
            SimpleMath::Matrix mInverse{};
        };

        struct BoneTransformBinding final {
            const Bone* mBone{};
            Transform* mTransform{};
        };

        using BoneTransformBindingMap = std::unordered_map<Arche::EntityID, BoneTransformBinding>;

    public:
        AnimateSystem() = default;
        ~AnimateSystem() override = default;
        AnimateSystem(const AnimateSystem& Other) = default;
        AnimateSystem& operator=(const AnimateSystem& Other) = default;
        AnimateSystem(AnimateSystem&& Other) noexcept = default;
        AnimateSystem& operator=(AnimateSystem&& Other) noexcept = default;

    public:
        const std::string& Name() const override;
        Phase GetPhase() const override;
        std::span<const ComponentAccess> ComponentAccesses() const override;
        std::span<const ResourceAccess> ResourceAccesses() const override;
        void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

    private:
        const std::vector<const asset::AnimationChannel*>& GetAnimationChannelLookup(const Model& TargetModel, const asset::AnimationClip& TargetClip);
        const SimpleMath::Matrix& GetNodeToParentInverse(const Model& TargetModel, std::uint32_t NodeIndex);
        void ApplyAnimatedPoseIterative(Arche::World& World, const BoneTransformBindingMap& BoneTransformBindings, Arche::EntityID RootId, const Model& TargetModel, std::span<const asset::AnimationChannel* const> SourceLookup, double SourceTick, std::span<const asset::AnimationChannel* const> DestinationLookup, double DestinationTick, float BlendAlpha);

    private:
        const std::string mName{ "AnimateSystem" };
        std::deque<AnimationChannelLookupCacheEntry> mAnimationChannelLookupCache{};
        std::deque<NodeToParentInverseCacheEntry> mNodeToParentInverseCache{};
    };
}
