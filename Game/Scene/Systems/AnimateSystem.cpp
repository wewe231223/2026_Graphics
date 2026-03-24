#include "AnimateSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Game/Model/Model.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/SkinnedMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    const asset::AnimationChannel* FindChannelByNodeName(const asset::AnimationClip& ClipData, const std::string& NodeName) {
        for (const asset::AnimationChannel& ChannelData : ClipData.Channels) {
            if (ChannelData.NodeName == NodeName) {
                return &ChannelData;
            }
        }

        return nullptr;
    }

    std::size_t ResolveFrameIndex(double AnimationTick, const std::vector<double>& Times) {
        if (Times.size() <= 1) {
            return 0;
        }

        for (std::size_t KeyIndex{ 0 }; KeyIndex + 1 < Times.size(); ++KeyIndex) {
            if (AnimationTick < Times[KeyIndex + 1]) {
                return KeyIndex;
            }
        }

        return Times.size() - 2;
    }

    float ResolveFactor(double AnimationTick, double Start, double End) {
        if (End <= Start) {
            return 0.0f;
        }

        const double RawFactor{ (AnimationTick - Start) / (End - Start) };
        return static_cast<float>(std::clamp(RawFactor, 0.0, 1.0));
    }

    SimpleMath::Vector3 SamplePosition(const asset::AnimationChannel& ChannelData, double AnimationTick) {
        if (ChannelData.PositionKeys.size() == 1) {
            const asset::Vec3& PositionValue{ ChannelData.PositionKeys.front().Value };
            return SimpleMath::Vector3{ PositionValue.x, PositionValue.y, PositionValue.z };
        }

        std::vector<double> Times{};
        Times.reserve(ChannelData.PositionKeys.size());
        for (const asset::AnimationKeyPosition& KeyData : ChannelData.PositionKeys) {
            Times.push_back(KeyData.Time);
        }

        const std::size_t StartIndex{ ResolveFrameIndex(AnimationTick, Times) };
        const std::size_t EndIndex{ StartIndex + 1 };
        const float BlendFactor{ ResolveFactor(AnimationTick, Times[StartIndex], Times[EndIndex]) };

        const asset::Vec3& StartValue{ ChannelData.PositionKeys[StartIndex].Value };
        const asset::Vec3& EndValue{ ChannelData.PositionKeys[EndIndex].Value };
        const SimpleMath::Vector3 StartPosition{ StartValue.x, StartValue.y, StartValue.z };
        const SimpleMath::Vector3 EndPosition{ EndValue.x, EndValue.y, EndValue.z };

        return SimpleMath::Vector3::Lerp(StartPosition, EndPosition, BlendFactor);
    }

    SimpleMath::Quaternion SampleRotation(const asset::AnimationChannel& ChannelData, double AnimationTick) {
        if (ChannelData.RotationKeys.size() == 1) {
            const asset::Vec4& RotationValue{ ChannelData.RotationKeys.front().Value };
            SimpleMath::Quaternion Result{ RotationValue.x, RotationValue.y, RotationValue.z, RotationValue.w };
            Result.Normalize();
            return Result;
        }

        std::vector<double> Times{};
        Times.reserve(ChannelData.RotationKeys.size());
        for (const asset::AnimationKeyRotation& KeyData : ChannelData.RotationKeys) {
            Times.push_back(KeyData.Time);
        }

        const std::size_t StartIndex{ ResolveFrameIndex(AnimationTick, Times) };
        const std::size_t EndIndex{ StartIndex + 1 };
        const float BlendFactor{ ResolveFactor(AnimationTick, Times[StartIndex], Times[EndIndex]) };

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
        if (ChannelData.ScaleKeys.size() == 1) {
            const asset::Vec3& ScaleValue{ ChannelData.ScaleKeys.front().Value };
            return SimpleMath::Vector3{ ScaleValue.x, ScaleValue.y, ScaleValue.z };
        }

        std::vector<double> Times{};
        Times.reserve(ChannelData.ScaleKeys.size());
        for (const asset::AnimationKeyScale& KeyData : ChannelData.ScaleKeys) {
            Times.push_back(KeyData.Time);
        }

        const std::size_t StartIndex{ ResolveFrameIndex(AnimationTick, Times) };
        const std::size_t EndIndex{ StartIndex + 1 };
        const float BlendFactor{ ResolveFactor(AnimationTick, Times[StartIndex], Times[EndIndex]) };

        const asset::Vec3& StartValue{ ChannelData.ScaleKeys[StartIndex].Value };
        const asset::Vec3& EndValue{ ChannelData.ScaleKeys[EndIndex].Value };
        const SimpleMath::Vector3 StartScale{ StartValue.x, StartValue.y, StartValue.z };
        const SimpleMath::Vector3 EndScale{ EndValue.x, EndValue.y, EndValue.z };

        return SimpleMath::Vector3::Lerp(StartScale, EndScale, BlendFactor);
    }

    void ApplyAnimatedPoseRecursive(Arche::World& World, Arche::EntityID EntityId, const Game::Model& ModelData, const asset::AnimationClip& ClipData, double AnimationTick) {
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
                const asset::AnimationChannel* ChannelData{ FindChannelByNodeName(ClipData, CurrentNode.GetName()) };

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

            ApplyAnimatedPoseRecursive(World, ChildEntityId, ModelData, ClipData, AnimationTick);
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

        for (auto [AnimatorComponent, BoneSkinReferenceComponent, SkinnedMeshRendererComponent, HierarchyComponent, TransformComponent] : World.Query<Animator, BoneSkinReference, SkinnedMeshRenderer, EntityHierarchy, Transform>()) {
            (void)HierarchyComponent;
            (void)TransformComponent;

            if (AnimatorComponent.animation == nullptr) {
                continue;
            }

            if (AnimatorComponent.clipIndex < 0) {
                continue;
            }

            const std::vector<asset::AnimationClip>& Clips{ AnimatorComponent.animation->Clips() };
            const std::size_t ClipIndex{ static_cast<std::size_t>(AnimatorComponent.clipIndex) };
            if (ClipIndex >= Clips.size()) {
                continue;
            }

            if (SkinnedMeshRendererComponent.model == nullptr || BoneSkinReferenceComponent.boneRootEntityId == Arche::NullEntityID) {
                continue;
            }

            const asset::AnimationClip& ClipData{ Clips[ClipIndex] };
            if (ClipData.Duration <= 0.0) {
                continue;
            }

            const double TicksPerSecond{ ClipData.TicksPerSecond > 0.0 ? ClipData.TicksPerSecond : 30.0 };
            AnimatorComponent.counter += static_cast<double>(Dt);

            const double DurationSeconds{ ClipData.Duration / TicksPerSecond };
            if (DurationSeconds > 0.0 && AnimatorComponent.counter >= DurationSeconds) {
                AnimatorComponent.counter = 0.0;
            }

            const double AnimationTick{ AnimatorComponent.counter * TicksPerSecond };
            ApplyAnimatedPoseRecursive(World, BoneSkinReferenceComponent.boneRootEntityId, *SkinnedMeshRendererComponent.model, ClipData, AnimationTick);
        }
    }
}
