#include "PipelineAnimateSystem.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <functional>
#include <iterator>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "Game/Model/Model.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/AnimatorGraphPlayer.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/SkinnedMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Pipeline/PipelineContext.h"

namespace Game {
    namespace Pipeline {
        namespace {
            struct ResolvedAnimator final {
            public:
                Arche::EntityID mEntityId{ Arche::NullEntityID };
                Animator* mComponent{};
            };

            struct ResolvedBoneSkinReference final {
            public:
                Arche::EntityID mEntityId{ Arche::NullEntityID };
                const BoneSkinReference* mComponent{};
            };

            struct BoneTransformBinding final {
            public:
                const Bone* mBone{};
                Transform* mTransform{};
            };

            struct PoseApplyCacheKey final {
            public:
                Arche::EntityID mAnimatorEntityId{ Arche::NullEntityID };
                const Model* mModel{};
                Arche::EntityID mBoneRootEntityId{ Arche::NullEntityID };
                std::int32_t mSourceClipIndex{ -1 };
                std::int32_t mDestinationClipIndex{ -1 };

                bool operator==(const PoseApplyCacheKey& Other) const = default;
            };

            struct PoseApplyCacheKeyHasher final {
            public:
                std::size_t operator()(const PoseApplyCacheKey& Value) const {
                    std::size_t Seed{};
                    HashCombine(Seed, PackEntityId(Value.mAnimatorEntityId));
                    HashCombine(Seed, Value.mModel);
                    HashCombine(Seed, PackEntityId(Value.mBoneRootEntityId));
                    HashCombine(Seed, Value.mSourceClipIndex);
                    HashCombine(Seed, Value.mDestinationClipIndex);
                    return Seed;
                }

            private:
                template <typename T>
                void HashCombine(std::size_t& InOutSeed, const T& Value) const {
                    InOutSeed ^= std::hash<T>{}(Value) + 0x9e3779b9u + (InOutSeed << 6u) + (InOutSeed >> 2u);
                }

                std::uint64_t PackEntityId(Arche::EntityID EntityId) const {
                    return (static_cast<std::uint64_t>(EntityId.generation) << 32ull) | EntityId.index;
                }
            };

            using ResolvedAnimatorCache = std::unordered_map<Arche::EntityID, ResolvedAnimator>;
            using ResolvedBoneSkinReferenceCache = std::unordered_map<Arche::EntityID, ResolvedBoneSkinReference>;
            using BoneTransformBindingMap = std::unordered_map<Arche::EntityID, BoneTransformBinding>;

            ResolvedAnimator ResolveAnimatorInHierarchy(PipelineContext& Ctx, Arche::EntityID StartEntityId, ResolvedAnimatorCache& Cache) {
                const ResolvedAnimatorCache::const_iterator CachedIter{ Cache.find(StartEntityId) };
                if (CachedIter != Cache.end()) {
                    return CachedIter->second;
                }

                Arche::EntityID CurrentEntityId{ StartEntityId };
                while (CurrentEntityId != Arche::NullEntityID && Ctx.ContainsEntity(CurrentEntityId) == true) {
                    const ResolvedAnimatorCache::const_iterator CurrentCachedIter{ Cache.find(CurrentEntityId) };
                    if (CurrentCachedIter != Cache.end()) {
                        Cache.insert_or_assign(StartEntityId, CurrentCachedIter->second);
                        return CurrentCachedIter->second;
                    }

                    Animator* AnimatorComponent{ Ctx.WriteComponent<Animator>(CurrentEntityId) };
                    if (AnimatorComponent != nullptr) {
                        ResolvedAnimator Result{ CurrentEntityId, AnimatorComponent };
                        Cache.insert_or_assign(CurrentEntityId, Result);
                        Cache.insert_or_assign(StartEntityId, Result);
                        return Result;
                    }

                    const EntityHierarchy* HierarchyComponent{ Ctx.ReadComponent<EntityHierarchy>(CurrentEntityId) };
                    if (HierarchyComponent == nullptr) {
                        break;
                    }

                    CurrentEntityId = HierarchyComponent->parent;
                }

                ResolvedAnimator Result{};
                Cache.insert_or_assign(StartEntityId, Result);
                return Result;
            }

            ResolvedBoneSkinReference ResolveBoneSkinReferenceInHierarchy(PipelineContext& Ctx, Arche::EntityID StartEntityId, ResolvedBoneSkinReferenceCache& Cache) {
                const ResolvedBoneSkinReferenceCache::const_iterator CachedIter{ Cache.find(StartEntityId) };
                if (CachedIter != Cache.end()) {
                    return CachedIter->second;
                }

                Arche::EntityID CurrentEntityId{ StartEntityId };
                while (CurrentEntityId != Arche::NullEntityID && Ctx.ContainsEntity(CurrentEntityId) == true) {
                    const ResolvedBoneSkinReferenceCache::const_iterator CurrentCachedIter{ Cache.find(CurrentEntityId) };
                    if (CurrentCachedIter != Cache.end()) {
                        Cache.insert_or_assign(StartEntityId, CurrentCachedIter->second);
                        return CurrentCachedIter->second;
                    }

                    const BoneSkinReference* BoneSkinReferenceComponent{ Ctx.ReadComponent<BoneSkinReference>(CurrentEntityId) };
                    if (BoneSkinReferenceComponent != nullptr) {
                        ResolvedBoneSkinReference Result{ CurrentEntityId, BoneSkinReferenceComponent };
                        Cache.insert_or_assign(CurrentEntityId, Result);
                        Cache.insert_or_assign(StartEntityId, Result);
                        return Result;
                    }

                    const EntityHierarchy* HierarchyComponent{ Ctx.ReadComponent<EntityHierarchy>(CurrentEntityId) };
                    if (HierarchyComponent == nullptr) {
                        break;
                    }

                    CurrentEntityId = HierarchyComponent->parent;
                }

                ResolvedBoneSkinReference Result{};
                Cache.insert_or_assign(StartEntityId, Result);
                return Result;
            }

            template <typename TKey>
            std::size_t ResolveFrameIndexFromKeys(double AnimationTick, const std::vector<TKey>& Keys) {
                if (Keys.size() <= 1u) {
                    return 0u;
                }

                const typename std::vector<TKey>::const_iterator KeyIter{ std::upper_bound(Keys.begin(), Keys.end(), AnimationTick, [](double LeftValue, const TKey& RightValue) { return LeftValue < RightValue.Time; }) };
                if (KeyIter == Keys.begin()) {
                    return 0u;
                }

                if (KeyIter == Keys.end()) {
                    return Keys.size() - 2u;
                }

                return static_cast<std::size_t>(std::distance(Keys.begin(), KeyIter) - 1);
            }

            float ResolveFactor(double Tick, double Start, double End) {
                return End <= Start ? 0.0f : static_cast<float>(std::clamp((Tick - Start) / (End - Start), 0.0, 1.0));
            }

            template <typename TKeys, typename TResult, typename TConvert, typename TLerp>
            TResult SampleTrack(const TKeys& Keys, double Tick, const TResult& Fallback, TConvert Convert, TLerp Lerp) {
                if (Keys.empty() == true) {
                    return Fallback;
                }

                if (Keys.size() == 1u) {
                    return Convert(Keys.front().Value);
                }

                const std::size_t KeyIndex{ ResolveFrameIndexFromKeys(Tick, Keys) };
                const float Alpha{ ResolveFactor(Tick, Keys[KeyIndex].Time, Keys[KeyIndex + 1u].Time) };
                return Lerp(Convert(Keys[KeyIndex].Value), Convert(Keys[KeyIndex + 1u].Value), Alpha);
            }

            SimpleMath::Vector3 SamplePosition(const asset::AnimationChannel& Channel, double Tick) {
                return SampleTrack(Channel.PositionKeys, Tick, SimpleMath::Vector3::Zero, [](const auto& Value) { return SimpleMath::Vector3{ Value.x, Value.y, Value.z }; }, [](const auto& LeftValue, const auto& RightValue, float Alpha) { return SimpleMath::Vector3::Lerp(LeftValue, RightValue, Alpha); });
            }

            SimpleMath::Quaternion SampleRotation(const asset::AnimationChannel& Channel, double Tick) {
                return SampleTrack(Channel.RotationKeys, Tick, SimpleMath::Quaternion::Identity, [](const auto& Value) { SimpleMath::Quaternion QuaternionValue{ Value.x, Value.y, Value.z, Value.w }; QuaternionValue.Normalize(); return QuaternionValue; }, [](const auto& LeftValue, const auto& RightValue, float Alpha) { return SimpleMath::Quaternion::Slerp(LeftValue, RightValue, Alpha); });
            }

            SimpleMath::Vector3 SampleScale(const asset::AnimationChannel& Channel, double Tick) {
                return SampleTrack(Channel.ScaleKeys, Tick, SimpleMath::Vector3{ 1.0f, 1.0f, 1.0f }, [](const auto& Value) { return SimpleMath::Vector3{ Value.x, Value.y, Value.z }; }, [](const auto& LeftValue, const auto& RightValue, float Alpha) { return SimpleMath::Vector3::Lerp(LeftValue, RightValue, Alpha); });
            }

            std::vector<const asset::AnimationChannel*> BuildAnimationChannelLookup(const Model& TargetModel, const asset::AnimationClip& TargetClip) {
                std::vector<const asset::AnimationChannel*> Lookup{};
                Lookup.resize(TargetModel.GetNodes().size(), nullptr);
                for (const asset::AnimationChannel& Channel : TargetClip.Channels) {
                    std::uint32_t NodeIndex{};
                    if (TargetModel.TryFindNodeIndexById(Channel.NodeId, NodeIndex) == true && NodeIndex < Lookup.size()) {
                        Lookup[NodeIndex] = &Channel;
                    }
                }

                return Lookup;
            }

            void BlendTransforms(SimpleMath::Vector3& InOutScale, SimpleMath::Quaternion& InOutRotation, SimpleMath::Vector3& InOutTranslation, const SimpleMath::Vector3& TargetScale, const SimpleMath::Quaternion& TargetRotation, const SimpleMath::Vector3& TargetTranslation, float Alpha) {
                InOutScale = SimpleMath::Vector3::Lerp(InOutScale, TargetScale, Alpha);
                InOutRotation = SimpleMath::Quaternion::Slerp(InOutRotation, TargetRotation, Alpha);
                InOutRotation.Normalize();
                InOutTranslation = SimpleMath::Vector3::Lerp(InOutTranslation, TargetTranslation, Alpha);
            }

            SimpleMath::Matrix ResolveNodeToParentInverse(const Model& TargetModel, std::uint32_t NodeIndex) {
                const std::vector<ModelNode>& Nodes{ TargetModel.GetNodes() };
                if (NodeIndex >= Nodes.size()) {
                    return SimpleMath::Matrix::Identity;
                }

                return Nodes[NodeIndex].GetNodeToParent().Invert();
            }

            void ApplyAnimatedPoseIterative(PipelineContext& Ctx, const BoneTransformBindingMap& BoneTransformBindings, Arche::EntityID RootEntityId, const Model& TargetModel, std::span<const asset::AnimationChannel* const> SourceLookup, double SourceTick, std::span<const asset::AnimationChannel* const> DestinationLookup, double DestinationTick, float BlendAlpha) {
                const std::vector<ModelNode>& Nodes{ TargetModel.GetNodes() };
                std::vector<Arche::EntityID> Stack{};
                Stack.reserve(256u);
                Stack.push_back(RootEntityId);

                while (Stack.empty() == false) {
                    const Arche::EntityID EntityId{ Stack.back() };
                    Stack.pop_back();
                    if (EntityId == Arche::NullEntityID || Ctx.ContainsEntity(EntityId) == false) {
                        continue;
                    }

                    const EntityHierarchy* HierarchyComponent{ Ctx.ReadComponent<EntityHierarchy>(EntityId) };
                    if (HierarchyComponent != nullptr) {
                        Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
                        while (ChildEntityId != Arche::NullEntityID) {
                            if (Ctx.ContainsEntity(ChildEntityId) == true) {
                                Stack.push_back(ChildEntityId);
                            }

                            const EntityHierarchy* ChildHierarchyComponent{ Ctx.ReadComponent<EntityHierarchy>(ChildEntityId) };
                            ChildEntityId = ChildHierarchyComponent == nullptr ? Arche::NullEntityID : ChildHierarchyComponent->nextSibling;
                        }
                    }

                    const Bone* BoneComponent{};
                    Transform* TransformComponent{};
                    const BoneTransformBindingMap::const_iterator BindingIter{ BoneTransformBindings.find(EntityId) };
                    if (BindingIter != BoneTransformBindings.end()) {
                        BoneComponent = BindingIter->second.mBone;
                        TransformComponent = BindingIter->second.mTransform;
                    }
                    else {
                        BoneComponent = Ctx.ReadComponent<Bone>(EntityId);
                        TransformComponent = Ctx.WriteComponent<Transform>(EntityId);
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

                    if (EntityId == RootEntityId) {
                        Translation = SimpleMath::Vector3::Zero;
                    }

                    const SimpleMath::Matrix AnimationLocal{ SimpleMath::Matrix::CreateScale(Scale) * SimpleMath::Matrix::CreateFromQuaternion(Rotation) * SimpleMath::Matrix::CreateTranslation(Translation) };
                    SimpleMath::Matrix DeltaLocal{ ResolveNodeToParentInverse(TargetModel, BoneComponent->nodeIndex) * AnimationLocal };
                    DeltaLocal.Decompose(TransformComponent->scale, TransformComponent->rotation, TransformComponent->position);
                    TransformComponent->UpdateEulerRadiansFromRotation();
                }
            }
        }

        PipelineAnimateSystem::PipelineAnimateSystem() {
        }

        PipelineAnimateSystem::~PipelineAnimateSystem() {
        }

        PipelineAnimateSystem::PipelineAnimateSystem(const PipelineAnimateSystem& Other) {
            (void)Other;
        }

        PipelineAnimateSystem& PipelineAnimateSystem::operator=(const PipelineAnimateSystem& Other) {
            (void)Other;
            return *this;
        }

        PipelineAnimateSystem::PipelineAnimateSystem(PipelineAnimateSystem&& Other) noexcept {
            (void)Other;
        }

        PipelineAnimateSystem& PipelineAnimateSystem::operator=(PipelineAnimateSystem&& Other) noexcept {
            (void)Other;
            return *this;
        }

        const std::string& PipelineAnimateSystem::Name() const {
            static const std::string NameText{ "AnimateSystem" };
            return NameText;
        }

        void PipelineAnimateSystem::Execute(PipelineContext& Ctx, float Dt) {
            ResolvedAnimatorCache AnimatorCache{};
            AnimatorCache.reserve(32u);
            ResolvedBoneSkinReferenceCache BoneSkinReferenceCache{};
            BoneSkinReferenceCache.reserve(32u);
            std::unordered_set<Arche::EntityID> UpdatedAnimators{};
            UpdatedAnimators.reserve(32u);
            std::unordered_set<PoseApplyCacheKey, PoseApplyCacheKeyHasher> AppliedPoses{};
            AppliedPoses.reserve(32u);
            BoneTransformBindingMap BoneTransformBindings{};
            BoneTransformBindings.reserve(256u);

            Ctx.ForEach<Bone, Transform, EntityHierarchy>([&](Arche::EntityID EntityId, Bone& BoneComponent, Transform& TransformComponent, EntityHierarchy& HierarchyComponent) {
                (void)HierarchyComponent;
                BoneTransformBindings.insert_or_assign(EntityId, BoneTransformBinding{ &BoneComponent, &TransformComponent });
            });

            Ctx.ForEach<SkinnedMeshRenderer, EntityHierarchy, Transform>([&](Arche::EntityID EntityId, SkinnedMeshRenderer& Renderer, EntityHierarchy& HierarchyComponent, Transform& TransformComponent) {
                (void)EntityId;
                (void)TransformComponent;

                ResolvedAnimator ResolvedAnimatorComponent{ ResolveAnimatorInHierarchy(Ctx, HierarchyComponent.self, AnimatorCache) };
                if (ResolvedAnimatorComponent.mComponent == nullptr || ResolvedAnimatorComponent.mComponent->animation == nullptr || Renderer.model == nullptr) {
                    return;
                }

                const ResolvedBoneSkinReference ResolvedBoneSkinReferenceComponent{ ResolveBoneSkinReferenceInHierarchy(Ctx, HierarchyComponent.self, BoneSkinReferenceCache) };
                if (ResolvedBoneSkinReferenceComponent.mComponent == nullptr) {
                    return;
                }

                const Arche::EntityID BoneRootEntityId{ ResolvedBoneSkinReferenceComponent.mComponent->boneRootEntityId };
                if (BoneRootEntityId == Arche::NullEntityID || Ctx.ContainsEntity(BoneRootEntityId) == false) {
                    return;
                }

                const std::vector<asset::AnimationClip>& Clips{ ResolvedAnimatorComponent.mComponent->animation->Clips() };
                AnimatorGraphPlayer* GraphPlayer{ Ctx.WriteComponent<AnimatorGraphPlayer>(ResolvedAnimatorComponent.mEntityId) };
                const std::int32_t SourceClipIndex{ GraphPlayer != nullptr ? GraphPlayer->SampleSourceClipIndex : ResolvedAnimatorComponent.mComponent->clipIndex };
                if (SourceClipIndex < 0 || static_cast<std::size_t>(SourceClipIndex) >= Clips.size()) {
                    return;
                }

                const std::int32_t DestinationClipIndex{ GraphPlayer != nullptr ? GraphPlayer->SampleDestinationClipIndex : SourceClipIndex };
                const float BlendAlpha{ GraphPlayer != nullptr ? std::clamp(GraphPlayer->SampleBlendAlpha, 0.0f, 1.0f) : 0.0f };
                const double SourceLocalTime{ GraphPlayer != nullptr ? GraphPlayer->SampleSourceLocalTime : ResolvedAnimatorComponent.mComponent->counter };
                const double DestinationLocalTime{ GraphPlayer != nullptr ? GraphPlayer->SampleDestinationLocalTime : ResolvedAnimatorComponent.mComponent->counter };
                PoseApplyCacheKey CacheKey{ ResolvedAnimatorComponent.mEntityId, Renderer.model, BoneRootEntityId, SourceClipIndex, DestinationClipIndex };
                if (AppliedPoses.insert(CacheKey).second == false) {
                    return;
                }

                const std::size_t SafeDestinationClipIndex{ DestinationClipIndex >= 0 && static_cast<std::size_t>(DestinationClipIndex) < Clips.size() ? static_cast<std::size_t>(DestinationClipIndex) : static_cast<std::size_t>(SourceClipIndex) };
                const asset::AnimationClip& SourceClip{ Clips[static_cast<std::size_t>(SourceClipIndex)] };
                const asset::AnimationClip& DestinationClip{ Clips[SafeDestinationClipIndex] };
                if (SourceClip.Duration <= 0.0) {
                    return;
                }

                const double SourceTicksPerSecond{ SourceClip.TicksPerSecond > 0.0 ? SourceClip.TicksPerSecond : 30.0 };
                const double DestinationTicksPerSecond{ DestinationClip.TicksPerSecond > 0.0 ? DestinationClip.TicksPerSecond : 30.0 };
                if (UpdatedAnimators.insert(ResolvedAnimatorComponent.mEntityId).second == true && GraphPlayer == nullptr) {
                    ResolvedAnimatorComponent.mComponent->counter += Dt;
                    const double DurationSeconds{ SourceClip.Duration / SourceTicksPerSecond };
                    if (DurationSeconds > 0.0) {
                        ResolvedAnimatorComponent.mComponent->counter = std::fmod(ResolvedAnimatorComponent.mComponent->counter, DurationSeconds);
                        if (ResolvedAnimatorComponent.mComponent->counter < 0.0) {
                            ResolvedAnimatorComponent.mComponent->counter += DurationSeconds;
                        }
                    }
                }

                const std::vector<const asset::AnimationChannel*> SourceLookup{ BuildAnimationChannelLookup(*Renderer.model, SourceClip) };
                const std::vector<const asset::AnimationChannel*> DestinationLookup{ BuildAnimationChannelLookup(*Renderer.model, DestinationClip) };
                ApplyAnimatedPoseIterative(Ctx, BoneTransformBindings, BoneRootEntityId, *Renderer.model, SourceLookup, SourceLocalTime * SourceTicksPerSecond, DestinationLookup, DestinationLocalTime * DestinationTicksPerSecond, BlendAlpha);
            });
        }
    }
}
