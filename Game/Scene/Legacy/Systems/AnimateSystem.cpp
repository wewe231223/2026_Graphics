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
#include <utility>

#include "Game/Model/Model.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/AnimatorGraphPlayer.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/SkinnedMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    template <class T>
    inline void HashCombine(std::size_t& seed, const T& v) {
        seed ^= std::hash<T>{}(v)+0x9e3779b9u + (seed << 6u) + (seed >> 2u);
    }

    inline std::uint64_t PackEntityID(Arche::EntityID id) {
        return (static_cast<std::uint64_t>(id.generation) << 32ull) | id.index;
    }

    struct ResolvedAnimator final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        Game::Animator* Component{ nullptr };
    };

    struct ResolvedBoneSkinReference final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        const Game::BoneSkinReference* Component{ nullptr };
    };

    struct PoseApplyCacheKey final {
        Arche::EntityID mAnimatorEntityId{ Arche::NullEntityID };
        const Game::Model* mModel{};
        Arche::EntityID mBoneRootEntityId{ Arche::NullEntityID };
        std::int32_t mSourceClipIndex{ -1 };
        std::int32_t mDestinationClipIndex{ -1 };

        bool operator==(const PoseApplyCacheKey& O) const = default;
    };

    struct PoseApplyCacheKeyHasher final {
        std::size_t operator()(const PoseApplyCacheKey& V) const {
            std::size_t seed{ 0u };
            HashCombine(seed, PackEntityID(V.mAnimatorEntityId));
            HashCombine(seed, V.mModel);
            HashCombine(seed, PackEntityID(V.mBoneRootEntityId));
            HashCombine(seed, V.mSourceClipIndex);
            HashCombine(seed, V.mDestinationClipIndex);
            return seed;
        }
    };

    using ResolvedAnimatorCache = std::unordered_map<Arche::EntityID, ResolvedAnimator>;
    using ResolvedBoneSkinReferenceCache = std::unordered_map<Arche::EntityID, ResolvedBoneSkinReference>;

    ResolvedAnimator ResolveAnimatorInHierarchy(Arche::World& World, Arche::EntityID startId, ResolvedAnimatorCache& Cache) {
        if (auto it = Cache.find(startId); it != Cache.end()) return it->second;

        Arche::EntityID currId = startId;
        while (currId != Arche::NullEntityID) {
            if (auto it = Cache.find(currId); it != Cache.end()) {
                return Cache[startId] = it->second;
            }

            if (auto* anim = World.GetComponent<Game::Animator>(currId)) {
                return Cache[startId] = { currId, anim };
            }

            if (const auto* Hierarchy{ std::as_const(World).GetComponent<Game::EntityHierarchy>(currId) }) {
                currId = Hierarchy->parent;
            }
            else {
                break;
            }
        }
        return Cache[startId] = {};
    }

    ResolvedBoneSkinReference ResolveBoneSkinReferenceInHierarchy(Arche::World& World, Arche::EntityID StartEntityId, ResolvedBoneSkinReferenceCache& Cache) {
        if (auto Iterator{ Cache.find(StartEntityId) }; Iterator != Cache.end()) {
            return Iterator->second;
        }

        Arche::EntityID CurrentEntityId{ StartEntityId };
        while (CurrentEntityId != Arche::NullEntityID) {
            if (auto Iterator{ Cache.find(CurrentEntityId) }; Iterator != Cache.end()) {
                Cache.insert_or_assign(StartEntityId, Iterator->second);
                return Iterator->second;
            }

            const Game::BoneSkinReference* BoneSkinReferenceComponent{ std::as_const(World).GetComponent<Game::BoneSkinReference>(CurrentEntityId) };
            if (BoneSkinReferenceComponent != nullptr) {
                ResolvedBoneSkinReference Result{ CurrentEntityId, BoneSkinReferenceComponent };
                Cache.insert_or_assign(CurrentEntityId, Result);
                Cache.insert_or_assign(StartEntityId, Result);
                return Result;
            }

            const Game::EntityHierarchy* HierarchyComponent{ std::as_const(World).GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (HierarchyComponent == nullptr) {
                break;
            }

            CurrentEntityId = HierarchyComponent->parent;
        }

        ResolvedBoneSkinReference Result{};
        Cache.insert_or_assign(StartEntityId, Result);
        return Result;
    }

    template<typename TKey>
    std::size_t ResolveFrameIndexFromKeys(double AnimationTick, const std::vector<TKey>& Keys) {
        if (Keys.size() <= 1) return 0;
        auto it = std::upper_bound(Keys.begin(), Keys.end(), AnimationTick, [](double L, const TKey& R) { return L < R.Time; });
        if (it == Keys.begin()) return 0;
        if (it == Keys.end()) return Keys.size() - 2;
        return static_cast<std::size_t>(std::distance(Keys.begin(), it) - 1);
    }

    float ResolveFactor(double Tick, double Start, double End) {
        return (End <= Start) ? 0.0f : static_cast<float>(std::clamp((Tick - Start) / (End - Start), 0.0, 1.0));
    }

    template<typename TKeys, typename TRet, typename TConv, typename TLerp>
    TRet SampleTrack(const TKeys& keys, double tick, const TRet& fallback, TConv conv, TLerp lerp) {
        if (keys.empty()) return fallback;
        if (keys.size() == 1) return conv(keys.front().Value);

        const std::size_t i = ResolveFrameIndexFromKeys(tick, keys);
        const float alpha = ResolveFactor(tick, keys[i].Time, keys[i + 1].Time);
        return lerp(conv(keys[i].Value), conv(keys[i + 1].Value), alpha);
    }

    SimpleMath::Vector3 SamplePosition(const asset::AnimationChannel& Ch, double Tick) {
        return SampleTrack(Ch.PositionKeys, Tick, SimpleMath::Vector3::Zero,
            [](const auto& v) { return SimpleMath::Vector3{ v.x, v.y, v.z }; },
            [](const auto& a, const auto& b, float t) { return SimpleMath::Vector3::Lerp(a, b, t); });
    }

    SimpleMath::Quaternion SampleRotation(const asset::AnimationChannel& Ch, double Tick) {
        return SampleTrack(Ch.RotationKeys, Tick, SimpleMath::Quaternion::Identity,
            [](const auto& v) { auto q = SimpleMath::Quaternion{ v.x, v.y, v.z, v.w }; q.Normalize(); return q; },
            [](const auto& a, const auto& b, float t) { return SimpleMath::Quaternion::Slerp(a, b, t); });
    }

    SimpleMath::Vector3 SampleScale(const asset::AnimationChannel& Ch, double Tick) {
        return SampleTrack(Ch.ScaleKeys, Tick, SimpleMath::Vector3{ 1.0f, 1.0f, 1.0f },
            [](const auto& v) { return SimpleMath::Vector3{ v.x, v.y, v.z }; },
            [](const auto& a, const auto& b, float t) { return SimpleMath::Vector3::Lerp(a, b, t); });
    }

    std::vector<const asset::AnimationChannel*> BuildAnimationChannelLookup(const Game::Model& Model, const asset::AnimationClip& Clip) {
        std::vector<const asset::AnimationChannel*> Lookup(Model.GetNodes().size(), nullptr);
        for (const auto& Ch : Clip.Channels) {
            std::uint32_t idx = 0;
            if (Model.TryFindNodeIndexById(Ch.NodeId, idx) && idx < Lookup.size()) {
                Lookup[idx] = &Ch;
            }
        }
        return Lookup;
    }

    void BlendTransforms(SimpleMath::Vector3& S, SimpleMath::Quaternion& R, SimpleMath::Vector3& T, const SimpleMath::Vector3& TS, const SimpleMath::Quaternion& TR, const SimpleMath::Vector3& TT, float Alpha) {
        S = SimpleMath::Vector3::Lerp(S, TS, Alpha);
        R = SimpleMath::Quaternion::Slerp(R, TR, Alpha);
        R.Normalize();
        T = SimpleMath::Vector3::Lerp(T, TT, Alpha);
    }

}

namespace Game {
    const std::string& AnimateSystem::Name() const { return mName; }
    Phase AnimateSystem::GetPhase() const { return Phase::Update; }

    std::span<const ComponentAccess> AnimateSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 7> Accesses{ {
            { typeid(Animator), Access::Write }, { typeid(AnimatorGraphPlayer), Access::Read },
            { typeid(BoneSkinReference), Access::Read }, { typeid(SkinnedMeshRenderer), Access::Read },
            { typeid(EntityHierarchy), Access::Read }, { typeid(Bone), Access::Read }, { typeid(Transform), Access::Read }
        } };
        return Accesses;
    }

    std::span<const ResourceAccess> AnimateSystem::ResourceAccesses() const { return {}; }

    const std::vector<const asset::AnimationChannel*>& AnimateSystem::GetAnimationChannelLookup(const Model& TargetModel, const asset::AnimationClip& TargetClip) {
        const std::vector<ModelNode>& Nodes{ TargetModel.GetNodes() };
        const ModelNode* NodeData{ Nodes.empty() == true ? nullptr : Nodes.data() };
        const std::size_t NodeCount{ Nodes.size() };
        const asset::AnimationChannel* ChannelData{ TargetClip.Channels.empty() == true ? nullptr : TargetClip.Channels.data() };
        const std::size_t ChannelCount{ TargetClip.Channels.size() };

        for (AnimationChannelLookupCacheEntry& CacheEntry : mAnimationChannelLookupCache) {
            if (CacheEntry.mModel != &TargetModel || CacheEntry.mClip != &TargetClip) {
                continue;
            }

            const bool IsCacheValid{ CacheEntry.mNodeData == NodeData && CacheEntry.mNodeCount == NodeCount && CacheEntry.mChannelData == ChannelData && CacheEntry.mChannelCount == ChannelCount && CacheEntry.mLookup.size() == NodeCount };
            if (IsCacheValid == false) {
                CacheEntry.mNodeData = NodeData;
                CacheEntry.mNodeCount = NodeCount;
                CacheEntry.mChannelData = ChannelData;
                CacheEntry.mChannelCount = ChannelCount;
                CacheEntry.mLookup = BuildAnimationChannelLookup(TargetModel, TargetClip);
            }

            return CacheEntry.mLookup;
        }

        AnimationChannelLookupCacheEntry NewEntry{};
        NewEntry.mModel = &TargetModel;
        NewEntry.mClip = &TargetClip;
        NewEntry.mNodeData = NodeData;
        NewEntry.mNodeCount = NodeCount;
        NewEntry.mChannelData = ChannelData;
        NewEntry.mChannelCount = ChannelCount;
        NewEntry.mLookup = BuildAnimationChannelLookup(TargetModel, TargetClip);
        mAnimationChannelLookupCache.push_back(std::move(NewEntry));
        return mAnimationChannelLookupCache.back().mLookup;
    }

    const SimpleMath::Matrix& AnimateSystem::GetNodeToParentInverse(const Model& TargetModel, std::uint32_t NodeIndex) {
        static const SimpleMath::Matrix IdentityMatrix{ SimpleMath::Matrix::Identity };

        const std::vector<ModelNode>& Nodes{ TargetModel.GetNodes() };
        if (NodeIndex >= Nodes.size()) {
            return IdentityMatrix;
        }

        const ModelNode* NodePointer{ &Nodes[NodeIndex] };
        for (NodeToParentInverseCacheEntry& CacheEntry : mNodeToParentInverseCache) {
            if (CacheEntry.mModel != &TargetModel || CacheEntry.mNodeIndex != NodeIndex) {
                continue;
            }

            if (CacheEntry.mNode != NodePointer) {
                CacheEntry.mNode = NodePointer;
                CacheEntry.mInverse = NodePointer->GetNodeToParent().Invert();
            }

            return CacheEntry.mInverse;
        }

        NodeToParentInverseCacheEntry NewEntry{};
        NewEntry.mModel = &TargetModel;
        NewEntry.mNode = NodePointer;
        NewEntry.mNodeIndex = NodeIndex;
        NewEntry.mInverse = NodePointer->GetNodeToParent().Invert();
        mNodeToParentInverseCache.push_back(NewEntry);
        return mNodeToParentInverseCache.back().mInverse;
    }

    void AnimateSystem::ApplyAnimatedPoseIterative(Arche::World& World, const BoneTransformBindingMap& BoneTransformBindings, Arche::EntityID RootId, const Model& TargetModel, std::span<const asset::AnimationChannel* const> SourceLookup, double SourceTick, std::span<const asset::AnimationChannel* const> DestinationLookup, double DestinationTick, float BlendAlpha) {
        const std::vector<ModelNode>& Nodes{ TargetModel.GetNodes() };
        std::vector<Arche::EntityID> Stack{};
        Stack.reserve(256);
        Stack.push_back(RootId);

        while (Stack.empty() == false) {
            const Arche::EntityID EntityId{ Stack.back() };
            Stack.pop_back();

            if (EntityId == Arche::NullEntityID) {
                continue;
            }

            const EntityHierarchy* HierarchyComponent{ std::as_const(World).GetComponent<EntityHierarchy>(EntityId) };
            if (HierarchyComponent != nullptr) {
                Arche::EntityID ChildId{ HierarchyComponent->firstChild };
                while (ChildId != Arche::NullEntityID) {
                    Stack.push_back(ChildId);
                    const EntityHierarchy* ChildHierarchyComponent{ std::as_const(World).GetComponent<EntityHierarchy>(ChildId) };
                    ChildId = ChildHierarchyComponent == nullptr ? Arche::NullEntityID : ChildHierarchyComponent->nextSibling;
                }
            }

            const Bone* BoneComponent{};
            Transform* TransformComponent{};
            const BoneTransformBindingMap::const_iterator BindingIterator{ BoneTransformBindings.find(EntityId) };
            if (BindingIterator != BoneTransformBindings.end()) {
                BoneComponent = BindingIterator->second.mBone;
                TransformComponent = BindingIterator->second.mTransform;
            }
            else {
                BoneComponent = std::as_const(World).GetComponent<Bone>(EntityId);
                TransformComponent = World.GetComponent<Transform>(EntityId);
            }

            if (BoneComponent == nullptr || TransformComponent == nullptr || BoneComponent->model != &TargetModel || BoneComponent->nodeIndex >= Nodes.size()) {
                continue;
            }

            const asset::AnimationChannel* SourceChannel{ SourceLookup[BoneComponent->nodeIndex] };
            const asset::AnimationChannel* DestinationChannel{ DestinationLookup[BoneComponent->nodeIndex] };

            if (SourceChannel == nullptr && DestinationChannel == nullptr) {
                continue;
            }

            SimpleMath::Vector3 Scale{ 1.0f, 1.0f, 1.0f };
            SimpleMath::Vector3 Translation{ 0.0f, 0.0f, 0.0f };
            SimpleMath::Quaternion Rotation{ SimpleMath::Quaternion::Identity };

            if (SourceChannel != nullptr) {
                Scale = SampleScale(*SourceChannel, SourceTick);
                Rotation = SampleRotation(*SourceChannel, SourceTick);
                Translation = SamplePosition(*SourceChannel, SourceTick);
            }

            if (DestinationChannel != nullptr && BlendAlpha == 0.0f) {
                Rotation.Normalize();
            }
            else if (DestinationChannel != nullptr) {
                BlendTransforms(Scale, Rotation, Translation, SampleScale(*DestinationChannel, DestinationTick), SampleRotation(*DestinationChannel, DestinationTick), SamplePosition(*DestinationChannel, DestinationTick), BlendAlpha);
            }

            if (EntityId == RootId) {
                Translation = SimpleMath::Vector3::Zero;
            }

            const SimpleMath::Matrix AnimationLocal{ SimpleMath::Matrix::CreateScale(Scale) * SimpleMath::Matrix::CreateFromQuaternion(Rotation) * SimpleMath::Matrix::CreateTranslation(Translation) };
            SimpleMath::Matrix DeltaLocal{ GetNodeToParentInverse(TargetModel, BoneComponent->nodeIndex) * AnimationLocal };

            DeltaLocal.Decompose(TransformComponent->scale, TransformComponent->rotation, TransformComponent->position);
            TransformComponent->UpdateEulerRadiansFromRotation();
        }
    }

    void AnimateSystem::Execute(Arche::World& World, FrameContext&, float Dt) {
        ResolvedAnimatorCache AnimatorCache{};
        AnimatorCache.reserve(32);

        ResolvedBoneSkinReferenceCache BoneSkinReferenceCache{};
        BoneSkinReferenceCache.reserve(32);

        std::unordered_set<Arche::EntityID> UpdatedAnimators{};
        UpdatedAnimators.reserve(32);

        std::unordered_set<PoseApplyCacheKey, PoseApplyCacheKeyHasher> AppliedPoses{};
        AppliedPoses.reserve(32);

        BoneTransformBindingMap BoneTransformBindings{};
        BoneTransformBindings.reserve(256);
        for (auto [BoneComponent, TransformComponent, HierarchyComponent] : World.Query<Bone, Transform, EntityHierarchy>()) {
            BoneTransformBindings.insert_or_assign(HierarchyComponent.self, BoneTransformBinding{ &BoneComponent, &TransformComponent });
        }

        for (auto [SMR, Hierarchy, Trans] : World.Query<SkinnedMeshRenderer, EntityHierarchy, Transform>()) {
            (void)Trans;
            auto Resolved = ResolveAnimatorInHierarchy(World, Hierarchy.self, AnimatorCache);
            if (!Resolved.Component || !Resolved.Component->animation || !SMR.model) continue;

            const ResolvedBoneSkinReference ResolvedBoneSkinReferenceComponent{ ResolveBoneSkinReferenceInHierarchy(World, Hierarchy.self, BoneSkinReferenceCache) };
            if (ResolvedBoneSkinReferenceComponent.Component == nullptr) continue;

            const Arche::EntityID BoneRootEntityId{ ResolvedBoneSkinReferenceComponent.Component->boneRootEntityId };
            if (BoneRootEntityId == Arche::NullEntityID) continue;

            const auto& Clips = Resolved.Component->animation->Clips();
            auto* GraphPlayer = World.GetComponent<AnimatorGraphPlayer>(Resolved.EntityId);

            const int32_t SrcClipIdx = GraphPlayer ? GraphPlayer->SampleSourceClipIndex : Resolved.Component->clipIndex;
            if (SrcClipIdx < 0 || static_cast<size_t>(SrcClipIdx) >= Clips.size()) continue;

            const int32_t DstClipIdx = GraphPlayer ? GraphPlayer->SampleDestinationClipIndex : SrcClipIdx;
            const float BlendAlpha = GraphPlayer ? std::clamp(GraphPlayer->SampleBlendAlpha, 0.0f, 1.0f) : 0.0f;
            const double SrcLocalTime = GraphPlayer ? GraphPlayer->SampleSourceLocalTime : Resolved.Component->counter;
            const double DstLocalTime = GraphPlayer ? GraphPlayer->SampleDestinationLocalTime : Resolved.Component->counter;

            PoseApplyCacheKey CacheKey{ Resolved.EntityId, SMR.model, BoneRootEntityId, SrcClipIdx, DstClipIdx };
            if (!AppliedPoses.insert(CacheKey).second) continue;

            const size_t SafeDstClipIdx = (DstClipIdx >= 0 && static_cast<size_t>(DstClipIdx) < Clips.size()) ? DstClipIdx : SrcClipIdx;
            const auto& SrcClip = Clips[SrcClipIdx];
            const auto& DstClip = Clips[SafeDstClipIdx];

            if (SrcClip.Duration <= 0.0) continue;

            const double SrcTPS = SrcClip.TicksPerSecond > 0.0 ? SrcClip.TicksPerSecond : 30.0;
            const double DstTPS = DstClip.TicksPerSecond > 0.0 ? DstClip.TicksPerSecond : 30.0;

            if (UpdatedAnimators.insert(Resolved.EntityId).second && !GraphPlayer) {
                Resolved.Component->counter += Dt;
                if (double DurSec = SrcClip.Duration / SrcTPS; DurSec > 0.0) {
                    Resolved.Component->counter = std::fmod(Resolved.Component->counter, DurSec);
                    if (Resolved.Component->counter < 0.0) Resolved.Component->counter += DurSec;
                }
            }

            const std::vector<const asset::AnimationChannel*>& SourceLookup{ GetAnimationChannelLookup(*SMR.model, SrcClip) };
            const std::vector<const asset::AnimationChannel*>& DestinationLookup{ GetAnimationChannelLookup(*SMR.model, DstClip) };
            ApplyAnimatedPoseIterative(World, BoneTransformBindings, BoneRootEntityId, *SMR.model, SourceLookup, SrcLocalTime * SrcTPS, DestinationLookup, DstLocalTime * DstTPS, BlendAlpha);
        }
    }
}
