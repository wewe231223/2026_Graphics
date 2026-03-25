#include "AnimateSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Game/Model/Model.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/SkinnedMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    struct ResolvedAnimator final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        Game::Animator* Component{ nullptr };
    };

    struct PoseApplyCacheKey final {
        Arche::EntityID mAnimatorEntityId{ Arche::NullEntityID };
        const Game::Model* mModel{};
        Arche::EntityID mBoneRootEntityId{ Arche::NullEntityID };

        bool operator==(const PoseApplyCacheKey& Other) const {
            return mAnimatorEntityId == Other.mAnimatorEntityId && mModel == Other.mModel && mBoneRootEntityId == Other.mBoneRootEntityId;
        }
    };

    struct PoseApplyCacheKeyHasher final {
        std::size_t operator()(const PoseApplyCacheKey& Value) const {
            std::size_t Seed{ 0u };
            const std::size_t AnimatorHash{ std::hash<std::uint64_t>{}((static_cast<std::uint64_t>(Value.mAnimatorEntityId.generation) << 32ull) | Value.mAnimatorEntityId.index) };
            const std::size_t ModelHash{ std::hash<const Game::Model*>{}(Value.mModel) };
            const std::size_t BoneRootHash{ std::hash<std::uint64_t>{}((static_cast<std::uint64_t>(Value.mBoneRootEntityId.generation) << 32ull) | Value.mBoneRootEntityId.index) };
            Seed ^= AnimatorHash + 0x9e3779b9u + (Seed << 6u) + (Seed >> 2u);
            Seed ^= ModelHash + 0x9e3779b9u + (Seed << 6u) + (Seed >> 2u);
            Seed ^= BoneRootHash + 0x9e3779b9u + (Seed << 6u) + (Seed >> 2u);
            return Seed;
        }
    };

    ResolvedAnimator ResolveAnimatorInHierarchy(Arche::World& World, const Game::EntityHierarchy& HierarchyComponent) {
        Arche::EntityID CurrentEntityId{ HierarchyComponent.self };
        while (CurrentEntityId != Arche::NullEntityID) {
            Game::Animator* AnimatorComponent{ World.GetComponent<Game::Animator>(CurrentEntityId) };
            if (AnimatorComponent != nullptr) {
                ResolvedAnimator Result{};
                Result.EntityId = CurrentEntityId;
                Result.Component = AnimatorComponent;
                return Result;
            }

            const Game::EntityHierarchy* CurrentHierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (CurrentHierarchyComponent == nullptr) {
                break;
            }

            CurrentEntityId = CurrentHierarchyComponent->parent;
        }

        return ResolvedAnimator{};
    }

    std::size_t ResolveFrameIndexFromPositionKeys(double AnimationTick, const std::vector<asset::AnimationKeyPosition>& Keys) {
        if (Keys.size() <= 1) {
            return 0;
        }

        const auto FirstIter{ Keys.begin() };
        const auto LastIter{ Keys.end() };
        const auto UpperBoundIter{ std::upper_bound(FirstIter, LastIter, AnimationTick, [](double Left, const asset::AnimationKeyPosition& Right) { return Left < Right.Time; }) };
        if (UpperBoundIter == FirstIter) {
            return 0;
        }

        if (UpperBoundIter == LastIter) {
            return Keys.size() - 2;
        }

        return static_cast<std::size_t>(std::distance(FirstIter, UpperBoundIter) - 1);
    }

    std::size_t ResolveFrameIndexFromRotationKeys(double AnimationTick, const std::vector<asset::AnimationKeyRotation>& Keys) {
        if (Keys.size() <= 1) {
            return 0;
        }

        const auto FirstIter{ Keys.begin() };
        const auto LastIter{ Keys.end() };
        const auto UpperBoundIter{ std::upper_bound(FirstIter, LastIter, AnimationTick, [](double Left, const asset::AnimationKeyRotation& Right) { return Left < Right.Time; }) };
        if (UpperBoundIter == FirstIter) {
            return 0;
        }

        if (UpperBoundIter == LastIter) {
            return Keys.size() - 2;
        }

        return static_cast<std::size_t>(std::distance(FirstIter, UpperBoundIter) - 1);
    }

    std::size_t ResolveFrameIndexFromScaleKeys(double AnimationTick, const std::vector<asset::AnimationKeyScale>& Keys) {
        if (Keys.size() <= 1) {
            return 0;
        }

        const auto FirstIter{ Keys.begin() };
        const auto LastIter{ Keys.end() };
        const auto UpperBoundIter{ std::upper_bound(FirstIter, LastIter, AnimationTick, [](double Left, const asset::AnimationKeyScale& Right) { return Left < Right.Time; }) };
        if (UpperBoundIter == FirstIter) {
            return 0;
        }

        if (UpperBoundIter == LastIter) {
            return Keys.size() - 2;
        }

        return static_cast<std::size_t>(std::distance(FirstIter, UpperBoundIter) - 1);
    }

    float ResolveFactor(double AnimationTick, double Start, double End) {
        if (End <= Start) {
            return 0.0f;
        }

        const double RawFactor{ (AnimationTick - Start) / (End - Start) };
        return static_cast<float>(std::clamp(RawFactor, 0.0, 1.0));
    }

    SimpleMath::Vector3 SamplePosition(const asset::AnimationChannel& ChannelData, double AnimationTick) {
        if (ChannelData.PositionKeys.empty()) {
            return SimpleMath::Vector3::Zero;
        }

        if (ChannelData.PositionKeys.size() == 1) {
            const asset::Vec3& PositionValue{ ChannelData.PositionKeys.front().Value };
            return SimpleMath::Vector3{ PositionValue.x, PositionValue.y, PositionValue.z };
        }

        const std::size_t StartIndex{ ResolveFrameIndexFromPositionKeys(AnimationTick, ChannelData.PositionKeys) };
        const std::size_t EndIndex{ StartIndex + 1 };
        const float BlendFactor{ ResolveFactor(AnimationTick, ChannelData.PositionKeys[StartIndex].Time, ChannelData.PositionKeys[EndIndex].Time) };

        const asset::Vec3& StartValue{ ChannelData.PositionKeys[StartIndex].Value };
        const asset::Vec3& EndValue{ ChannelData.PositionKeys[EndIndex].Value };
        const SimpleMath::Vector3 StartPosition{ StartValue.x, StartValue.y, StartValue.z };
        const SimpleMath::Vector3 EndPosition{ EndValue.x, EndValue.y, EndValue.z };

        return SimpleMath::Vector3::Lerp(StartPosition, EndPosition, BlendFactor);
    }

    SimpleMath::Quaternion SampleRotation(const asset::AnimationChannel& ChannelData, double AnimationTick) {
        if (ChannelData.RotationKeys.empty()) {
            return SimpleMath::Quaternion::Identity;
        }

        if (ChannelData.RotationKeys.size() == 1) {
            const asset::Vec4& RotationValue{ ChannelData.RotationKeys.front().Value };
            SimpleMath::Quaternion Result{ RotationValue.x, RotationValue.y, RotationValue.z, RotationValue.w };
            Result.Normalize();
            return Result;
        }

        const std::size_t StartIndex{ ResolveFrameIndexFromRotationKeys(AnimationTick, ChannelData.RotationKeys) };
        const std::size_t EndIndex{ StartIndex + 1 };
        const float BlendFactor{ ResolveFactor(AnimationTick, ChannelData.RotationKeys[StartIndex].Time, ChannelData.RotationKeys[EndIndex].Time) };

        const asset::Vec4& StartValue{ ChannelData.RotationKeys[StartIndex].Value };
        const asset::Vec4& EndValue{ ChannelData.RotationKeys[EndIndex].Value };
        SimpleMath::Quaternion StartRotation{ StartValue.x, StartValue.y, StartValue.z, StartValue.w };
        SimpleMath::Quaternion EndRotation{ EndValue.x, EndValue.y, EndValue.z, EndValue.w };
        StartRotation.Normalize();
        EndRotation.Normalize();

        SimpleMath::Quaternion Result{ SimpleMath::Quaternion::Slerp(StartRotation, EndRotation, BlendFactor) };
        Result.Normalize();
        return Result;
    }

    SimpleMath::Vector3 SampleScale(const asset::AnimationChannel& ChannelData, double AnimationTick) {
        if (ChannelData.ScaleKeys.empty()) {
            return SimpleMath::Vector3{ 1.0f, 1.0f, 1.0f };
        }

        if (ChannelData.ScaleKeys.size() == 1) {
            const asset::Vec3& ScaleValue{ ChannelData.ScaleKeys.front().Value };
            return SimpleMath::Vector3{ ScaleValue.x, ScaleValue.y, ScaleValue.z };
        }

        const std::size_t StartIndex{ ResolveFrameIndexFromScaleKeys(AnimationTick, ChannelData.ScaleKeys) };
        const std::size_t EndIndex{ StartIndex + 1 };
        const float BlendFactor{ ResolveFactor(AnimationTick, ChannelData.ScaleKeys[StartIndex].Time, ChannelData.ScaleKeys[EndIndex].Time) };

        const asset::Vec3& StartValue{ ChannelData.ScaleKeys[StartIndex].Value };
        const asset::Vec3& EndValue{ ChannelData.ScaleKeys[EndIndex].Value };
        const SimpleMath::Vector3 StartScale{ StartValue.x, StartValue.y, StartValue.z };
        const SimpleMath::Vector3 EndScale{ EndValue.x, EndValue.y, EndValue.z };

        return SimpleMath::Vector3::Lerp(StartScale, EndScale, BlendFactor);
    }

    std::vector<const asset::AnimationChannel*> BuildAnimationChannelLookup(const Game::Model& ModelData, const asset::AnimationClip& ClipData) {
        const std::vector<Game::ModelNode>& Nodes{ ModelData.GetNodes() };
        std::vector<const asset::AnimationChannel*> ChannelLookup{};
        ChannelLookup.resize(Nodes.size(), nullptr);

        for (const asset::AnimationChannel& ChannelData : ClipData.Channels) {
            std::uint32_t NodeIndex{ 0 };
            const bool IsNodeIndexFound{ ModelData.TryFindNodeIndexById(ChannelData.NodeId, NodeIndex) };
            if (IsNodeIndexFound == false) {
                continue;
            }

            const std::size_t LookupIndex{ static_cast<std::size_t>(NodeIndex) };
            if (LookupIndex >= ChannelLookup.size()) {
                continue;
            }

            ChannelLookup[LookupIndex] = &ChannelData;
        }

        return ChannelLookup;
    }

    void ApplyAnimatedPoseRecursive(Arche::World& World, Arche::EntityID EntityId, const Game::Model& ModelData, std::span<const asset::AnimationChannel* const> ChannelLookup, double AnimationTick) {
        if (EntityId == Arche::NullEntityID) {
            return;
        }

        const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(EntityId) };
        if (HierarchyComponent == nullptr) {
            return;
        }

        const Game::Bone* BoneComponent{ World.GetComponent<Game::Bone>(EntityId) };
        Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(EntityId) };

        if (BoneComponent != nullptr && TransformComponent != nullptr && BoneComponent->model == &ModelData) {
            const std::vector<Game::ModelNode>& Nodes{ ModelData.GetNodes() };
            if (BoneComponent->nodeIndex < Nodes.size()) {
                const std::uint32_t NodeIndex{ BoneComponent->nodeIndex };
                const Game::ModelNode& CurrentNode{ Nodes[NodeIndex] };
                const asset::AnimationChannel* ChannelData{ ChannelLookup[NodeIndex] };

                if (ChannelData != nullptr) {

                    SimpleMath::Matrix BindLocal = CurrentNode.GetNodeToParent();

                    SimpleMath::Vector3 S = SampleScale(*ChannelData, AnimationTick);
                    SimpleMath::Quaternion R = SampleRotation(*ChannelData, AnimationTick);
                    SimpleMath::Vector3 T = SamplePosition(*ChannelData, AnimationTick);

                    SimpleMath::Matrix AnimLocal = SimpleMath::Matrix::CreateScale(S) * SimpleMath::Matrix::CreateFromQuaternion(R) * SimpleMath::Matrix::CreateTranslation(T);

                    SimpleMath::Matrix DeltaLocal = BindLocal.Invert() * AnimLocal;

                    DeltaLocal.Decompose(TransformComponent->scale, TransformComponent->rotation, TransformComponent->position);
                }
            }
        }

        Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
        while (ChildEntityId != Arche::NullEntityID) {
            const Game::EntityHierarchy* ChildHierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(ChildEntityId) };
            if (ChildHierarchyComponent == nullptr) {
                break;
            }

            ApplyAnimatedPoseRecursive(World, ChildEntityId, ModelData, ChannelLookup, AnimationTick);
            ChildEntityId = ChildHierarchyComponent->nextSibling;
        }
    }
}

namespace Game {
    const std::string& AnimateSystem::Name() const {
        return mName;
    }

    Phase AnimateSystem::GetPhase() const {
        return Phase::Update;
    }

    std::span<const ComponentAccess> AnimateSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 6> Accesses{ { { typeid(Animator), Access::Write }, { typeid(BoneSkinReference), Access::Read }, { typeid(SkinnedMeshRenderer), Access::Read }, { typeid(EntityHierarchy), Access::Read }, { typeid(Bone), Access::Read }, { typeid(Transform), Access::Write } } };
        return Accesses;
    }

    std::span<const ResourceAccess> AnimateSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 0> Accesses{};
        return Accesses;
    }

    void AnimateSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Ctx;
        std::unordered_set<Arche::EntityID> UpdatedAnimatorEntityIds{};
        UpdatedAnimatorEntityIds.reserve(32);
        std::unordered_map<PoseApplyCacheKey, std::vector<const asset::AnimationChannel*>, PoseApplyCacheKeyHasher> ChannelLookupCache{};
        ChannelLookupCache.reserve(32);
        std::unordered_set<PoseApplyCacheKey, PoseApplyCacheKeyHasher> AppliedPoseKeys{};
        AppliedPoseKeys.reserve(32);

        for (auto [BoneSkinReferenceComponent, SkinnedMeshRendererComponent, HierarchyComponent, TransformComponent] : World.Query<BoneSkinReference, SkinnedMeshRenderer, EntityHierarchy, Transform>()) {
            (void)TransformComponent;

            const ResolvedAnimator ResolvedAnimatorComponent{ ResolveAnimatorInHierarchy(World, HierarchyComponent) };
            Animator* AnimatorComponent{ ResolvedAnimatorComponent.Component };
            if (AnimatorComponent == nullptr || AnimatorComponent->animation == nullptr) {
                continue;
            }

            if (AnimatorComponent->clipIndex < 0) {
                continue;
            }

            const std::vector<asset::AnimationClip>& Clips{ AnimatorComponent->animation->Clips() };
            const std::size_t ClipIndex{ static_cast<std::size_t>(AnimatorComponent->clipIndex) };
            if (ClipIndex >= Clips.size()) {
                continue;
            }

            if (SkinnedMeshRendererComponent.model == nullptr || BoneSkinReferenceComponent.boneRootEntityId == Arche::NullEntityID) {
                continue;
            }

            PoseApplyCacheKey CacheKey{};
            CacheKey.mAnimatorEntityId = ResolvedAnimatorComponent.EntityId;
            CacheKey.mModel = SkinnedMeshRendererComponent.model;
            CacheKey.mBoneRootEntityId = BoneSkinReferenceComponent.boneRootEntityId;
            const std::pair<std::unordered_set<PoseApplyCacheKey, PoseApplyCacheKeyHasher>::iterator, bool> AppliedResult{ AppliedPoseKeys.insert(CacheKey) };
            if (AppliedResult.second == false) {
                continue;
            }

            const asset::AnimationClip& ClipData{ Clips[ClipIndex] };
            if (ClipData.Duration <= 0.0) {
                continue;
            }

            const double TicksPerSecond{ ClipData.TicksPerSecond > 0.0 ? ClipData.TicksPerSecond : 30.0 };
            const std::pair<std::unordered_set<Arche::EntityID>::iterator, bool> InsertResult{ UpdatedAnimatorEntityIds.insert(ResolvedAnimatorComponent.EntityId) };
            if (InsertResult.second == true) {
                AnimatorComponent->counter += static_cast<double>(Dt);

                const double DurationSeconds{ ClipData.Duration / TicksPerSecond };
                if (DurationSeconds > 0.0) {
                    AnimatorComponent->counter = std::fmod(AnimatorComponent->counter, DurationSeconds);
                    if (AnimatorComponent->counter < 0.0) {
                        AnimatorComponent->counter += DurationSeconds;
                    }
                }
            }

            const double AnimationTick{ AnimatorComponent->counter * TicksPerSecond };
            std::unordered_map<PoseApplyCacheKey, std::vector<const asset::AnimationChannel*>, PoseApplyCacheKeyHasher>::iterator ChannelLookupCacheIter{ ChannelLookupCache.find(CacheKey) };
            if (ChannelLookupCacheIter == ChannelLookupCache.end()) {
                std::vector<const asset::AnimationChannel*> NewChannelLookup{ BuildAnimationChannelLookup(*SkinnedMeshRendererComponent.model, ClipData) };
                const std::pair<std::unordered_map<PoseApplyCacheKey, std::vector<const asset::AnimationChannel*>, PoseApplyCacheKeyHasher>::iterator, bool> InsertedResult{ ChannelLookupCache.emplace(CacheKey, std::move(NewChannelLookup)) };
                ChannelLookupCacheIter = InsertedResult.first;
            }

            ApplyAnimatedPoseRecursive(World, BoneSkinReferenceComponent.boneRootEntityId, *SkinnedMeshRendererComponent.model, ChannelLookupCacheIter->second, AnimationTick);
        }
    }
}
