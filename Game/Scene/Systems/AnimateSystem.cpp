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
#include "Game/Scene/Components/AnimatorGraphPlayer.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/SkinnedMeshRenderer.h"
#include "Game/Scene/Components/RootMotion.h"
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

        bool operator==(const PoseApplyCacheKey& O) const {
            return mAnimatorEntityId == O.mAnimatorEntityId && mModel == O.mModel &&
                mBoneRootEntityId == O.mBoneRootEntityId && mSourceClipIndex == O.mSourceClipIndex &&
                mDestinationClipIndex == O.mDestinationClipIndex;
        }
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

    struct AnimationChannelLookupCacheKey final {
        const Game::Model* mModel{};
        const asset::AnimationClip* mClip{};

        bool operator==(const AnimationChannelLookupCacheKey& O) const {
            return mModel == O.mModel && mClip == O.mClip;
        }
    };

    struct AnimationChannelLookupCacheKeyHasher final {
        std::size_t operator()(const AnimationChannelLookupCacheKey& V) const {
            std::size_t seed{ 0u };
            HashCombine(seed, V.mModel);
            HashCombine(seed, V.mClip);
            return seed;
        }
    };

    using ResolvedAnimatorCache = std::unordered_map<Arche::EntityID, ResolvedAnimator>;

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

            if (auto* hierarchy = World.GetComponent<Game::EntityHierarchy>(currId)) {
                currId = hierarchy->parent;
            }
            else {
                break;
            }
        }
        return Cache[startId] = {};
    }

    ResolvedBoneSkinReference ResolveBoneSkinReferenceInHierarchy(Arche::World& World, Arche::EntityID StartEntityId) {
        Arche::EntityID CurrentEntityId{ StartEntityId };
        while (CurrentEntityId != Arche::NullEntityID) {
            const Game::BoneSkinReference* BoneSkinReferenceComponent{ World.GetComponent<Game::BoneSkinReference>(CurrentEntityId) };
            if (BoneSkinReferenceComponent != nullptr) {
                return ResolvedBoneSkinReference{ CurrentEntityId, BoneSkinReferenceComponent };
            }

            const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (HierarchyComponent == nullptr) {
                break;
            }

            CurrentEntityId = HierarchyComponent->parent;
        }

        return ResolvedBoneSkinReference{};
    }

    SimpleMath::Matrix BuildLocalWorldMatrix(const Game::Transform& TransformComponent) {
        const SimpleMath::Matrix TrsMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
        return TransformComponent.nodeToParent * TrsMatrix;
    }

    bool TryResolveWorldPositionFromNodeToParent(Arche::World& World, Arche::EntityID EntityId, SimpleMath::Vector3& OutWorldPosition) {
        std::vector<Arche::EntityID> EntityPath{};
        Arche::EntityID CurrentEntityId{ EntityId };

        while (CurrentEntityId != Arche::NullEntityID) {
            const Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(CurrentEntityId) };
            const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (TransformComponent == nullptr || HierarchyComponent == nullptr) {
                return false;
            }

            EntityPath.push_back(CurrentEntityId);
            CurrentEntityId = HierarchyComponent->parent;
        }

        SimpleMath::Matrix CurrentWorldMatrix{ SimpleMath::Matrix::Identity };
        for (std::vector<Arche::EntityID>::const_reverse_iterator EntityPathIter{ EntityPath.crbegin() }; EntityPathIter != EntityPath.crend(); ++EntityPathIter) {
            const Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(*EntityPathIter) };
            if (TransformComponent == nullptr) {
                return false;
            }

            const SimpleMath::Matrix LocalNodeToParentMatrix{ BuildLocalWorldMatrix(*TransformComponent) };
            CurrentWorldMatrix = LocalNodeToParentMatrix * CurrentWorldMatrix;
        }

        OutWorldPosition = SimpleMath::Vector3{ CurrentWorldMatrix._41, CurrentWorldMatrix._42, CurrentWorldMatrix._43 };
        return true;
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

    void ApplyAnimatedPoseIterative(Arche::World& World, Arche::EntityID RootId, const Game::Model& Model, std::span<const asset::AnimationChannel* const> SrcLookup, double SrcTick, std::span<const asset::AnimationChannel* const> DstLookup, double DstTick, float BlendAlpha) {
        const auto& Nodes = Model.GetNodes();
        std::vector<Arche::EntityID> Stack;
        Stack.reserve(256);
        Stack.push_back(RootId);

        while (!Stack.empty()) {
            const Arche::EntityID EntityId = Stack.back();
            Stack.pop_back();

            if (EntityId == Arche::NullEntityID) continue;

            if (auto* Hierarchy = World.GetComponent<Game::EntityHierarchy>(EntityId)) {
                Arche::EntityID ChildId = Hierarchy->firstChild;
                std::vector<Arche::EntityID> Children;
                while (ChildId != Arche::NullEntityID) {
                    Children.push_back(ChildId);
                    auto* ChildHierarchy = World.GetComponent<Game::EntityHierarchy>(ChildId);
                    ChildId = ChildHierarchy ? ChildHierarchy->nextSibling : Arche::NullEntityID;
                }
                for (auto it = Children.rbegin(); it != Children.rend(); ++it) {
                    Stack.push_back(*it);
                }
            }

            auto* Bone = World.GetComponent<Game::Bone>(EntityId);
            auto* Transform = World.GetComponent<Game::Transform>(EntityId);

            if (!Bone || !Transform || Bone->model != &Model || Bone->nodeIndex >= Nodes.size()) continue;

            const auto* SrcCh = SrcLookup[Bone->nodeIndex];
            const auto* DstCh = DstLookup[Bone->nodeIndex];

            if (!SrcCh && !DstCh) continue;

            SimpleMath::Vector3 S{ 1.0f, 1.0f, 1.0f }, T{ 0.0f, 0.0f, 0.0f };
            SimpleMath::Quaternion R = SimpleMath::Quaternion::Identity;

            if (SrcCh) {
                S = SampleScale(*SrcCh, SrcTick);
                R = SampleRotation(*SrcCh, SrcTick);
                T = SamplePosition(*SrcCh, SrcTick);
            }

            if (DstCh) {
                BlendTransforms(S, R, T, SampleScale(*DstCh, DstTick), SampleRotation(*DstCh, DstTick), SamplePosition(*DstCh, DstTick), BlendAlpha);
            }

            SimpleMath::Matrix AnimLocal = SimpleMath::Matrix::CreateScale(S) * SimpleMath::Matrix::CreateFromQuaternion(R) * SimpleMath::Matrix::CreateTranslation(T);
            SimpleMath::Matrix DeltaLocal = Nodes[Bone->nodeIndex].GetNodeToParent().Invert() * AnimLocal;

            DeltaLocal.Decompose(Transform->scale, Transform->rotation, Transform->position);
            Transform->UpdateEulerRadiansFromRotation();
        }
    }
}

namespace Game {
    const std::string& AnimateSystem::Name() const { return mName; }
    Phase AnimateSystem::GetPhase() const { return Phase::Update; }

    std::span<const ComponentAccess> AnimateSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 8> Accesses{ {
            { typeid(Animator), Access::Write }, { typeid(AnimatorGraphPlayer), Access::Read },
            { typeid(BoneSkinReference), Access::Read }, { typeid(SkinnedMeshRenderer), Access::Read },
            { typeid(EntityHierarchy), Access::Read }, { typeid(Bone), Access::Read }, { typeid(Transform), Access::Read }, { typeid(RootMotion), Access::Write }
        } };
        return Accesses;
    }

    std::span<const ResourceAccess> AnimateSystem::ResourceAccesses() const { return {}; }

    void AnimateSystem::Execute(Arche::World& World, FrameContext&, float Dt) {
        ResolvedAnimatorCache AnimatorCache{};
        AnimatorCache.reserve(32);

        std::unordered_set<Arche::EntityID> UpdatedAnimators{};
        UpdatedAnimators.reserve(32);

        std::unordered_map<AnimationChannelLookupCacheKey, std::vector<const asset::AnimationChannel*>, AnimationChannelLookupCacheKeyHasher> ChannelCache{};
        ChannelCache.reserve(32);

        std::unordered_set<PoseApplyCacheKey, PoseApplyCacheKeyHasher> AppliedPoses{};
        AppliedPoses.reserve(32);

        std::unordered_set<Arche::EntityID> UpdatedRootMotions{};
        UpdatedRootMotions.reserve(64);

        for (auto [RootMotionComponent] : World.Query<RootMotion>()) {
            RootMotionComponent.previousRootBoneWorldPosition = RootMotionComponent.rootBoneWorldPosition;
            RootMotionComponent.hasPreviousRootBoneWorldPosition = RootMotionComponent.hasRootBoneWorldPosition;
            RootMotionComponent.rootBoneWorldPosition = SimpleMath::Vector3::Zero;
            RootMotionComponent.hasRootBoneWorldPosition = false;
        }

        for (auto [SMR, Hierarchy, Trans] : World.Query<SkinnedMeshRenderer, EntityHierarchy, Transform>()) {
            (void)Trans;
            auto Resolved = ResolveAnimatorInHierarchy(World, Hierarchy.self, AnimatorCache);
            if (!Resolved.Component || !Resolved.Component->animation || !SMR.model) continue;

            const ResolvedBoneSkinReference ResolvedBoneSkinReferenceComponent{ ResolveBoneSkinReferenceInHierarchy(World, Hierarchy.self) };
            if (ResolvedBoneSkinReferenceComponent.Component == nullptr) continue;

            const Arche::EntityID BoneRootEntityId{ ResolvedBoneSkinReferenceComponent.Component->boneRootEntityId };
            if (BoneRootEntityId == Arche::NullEntityID) continue;

            if (UpdatedRootMotions.contains(BoneRootEntityId) == false) {
                SimpleMath::Vector3 RootBoneWorldPosition{};
                const bool IsRootBoneWorldPositionResolved{ TryResolveWorldPositionFromNodeToParent(World, BoneRootEntityId, RootBoneWorldPosition) };

                RootMotion* RootMotionComponent{ World.GetComponent<RootMotion>(BoneRootEntityId) };
                if (RootMotionComponent == nullptr) {
                    RootMotion NewRootMotion{};
                    World.AddComponent(BoneRootEntityId, NewRootMotion);
                    RootMotionComponent = World.GetComponent<RootMotion>(BoneRootEntityId);
                }

                if (RootMotionComponent != nullptr) {
                    if (IsRootBoneWorldPositionResolved == true) {
                        RootMotionComponent->rootBoneWorldPosition = RootBoneWorldPosition;
                        RootMotionComponent->hasRootBoneWorldPosition = true;
                    }
                    else {
                        RootMotionComponent->rootBoneWorldPosition = SimpleMath::Vector3::Zero;
                        RootMotionComponent->hasRootBoneWorldPosition = false;
                    }
                }

                UpdatedRootMotions.insert(BoneRootEntityId);
            }

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

            auto GetLookup = [&](const asset::AnimationClip& clip) {
                AnimationChannelLookupCacheKey key{ SMR.model, &clip };
                if (auto it = ChannelCache.find(key); it != ChannelCache.end()) return it->second;
                return ChannelCache.emplace(key, BuildAnimationChannelLookup(*SMR.model, clip)).first->second;
                };

            ApplyAnimatedPoseIterative(World, BoneRootEntityId, *SMR.model,
                GetLookup(SrcClip), SrcLocalTime * SrcTPS,
                GetLookup(DstClip), DstLocalTime * DstTPS, BlendAlpha);
        }
    }
}
