#include "Game/Scene/Systems/FootIKSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/FootIKRig.h"
#include "Game/Scene/Components/FootIKRuntime.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/TerrainCollidee.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/IK/FabrikFootIKSolver.h"
#include "Game/Scene/IK/FootIKAlgorithms.h"
#include "Game/Scene/IK/FootIKSolver.h"

namespace {
    bool IsFiniteFloat(const float Value) {
        return ::std::isfinite(Value) != 0;
    }

    void AppendGroundSampleDebugLine(Game::FrameContext& Ctx, const SimpleMath::Vector3& GroundSamplePosition, const SimpleMath::Vector3& GroundNormal) {
        constexpr float GroundSampleMarkerLength{ 0.5f };
        constexpr float GroundSampleMarkerLineThickness{ 0.0035f };
        const SimpleMath::Vector3 MarkerStart{ GroundSamplePosition };
        const SimpleMath::Vector3 MarkerEnd{ GroundSamplePosition + (GroundNormal * GroundSampleMarkerLength) };
        Ctx.RenderData.debugGeometryContexts.push_back(Game::RFD::DebugGeometryContext::CreateLine(MarkerStart, MarkerEnd, SimpleMath::Vector4{ 1.0f, 0.1f, 0.1f, 1.0f }, GroundSampleMarkerLineThickness));
    }
}

namespace Game {
    FootIKSystem::FootIKSystem()
        : mName{ "FootIKSystem" },
          mFootIKSolver{ CreateFabrikFootIKSolver() } {
    }

    FootIKSystem::~FootIKSystem() {
    }

    FootIKSystem::FootIKSystem(const FootIKSystem& Other)
        : mName{ Other.mName },
          mFootIKSolver{ Other.mFootIKSolver == nullptr ? CreateFabrikFootIKSolver() : Other.mFootIKSolver->Clone() } {
    }

    FootIKSystem& FootIKSystem::operator=(const FootIKSystem& Other) {
        if (this == &Other) {
            return *this;
        }

        mName = Other.mName;
        mFootIKSolver = Other.mFootIKSolver == nullptr ? CreateFabrikFootIKSolver() : Other.mFootIKSolver->Clone();
        return *this;
    }

    FootIKSystem::FootIKSystem(FootIKSystem&& Other) noexcept
        : mName{ ::std::move(Other.mName) },
          mFootIKSolver{ ::std::move(Other.mFootIKSolver) } {
        if (mFootIKSolver == nullptr) {
            mFootIKSolver = CreateFabrikFootIKSolver();
        }
    }

    FootIKSystem& FootIKSystem::operator=(FootIKSystem&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mName = ::std::move(Other.mName);
        mFootIKSolver = ::std::move(Other.mFootIKSolver);
        if (mFootIKSolver == nullptr) {
            mFootIKSolver = CreateFabrikFootIKSolver();
        }

        return *this;
    }

    const std::string& FootIKSystem::Name() const {
        return mName;
    }

    Phase FootIKSystem::GetPhase() const {
        return Phase::IK;
    }

    std::span<const ComponentAccess> FootIKSystem::ComponentAccesses() const {
        static ::std::array<ComponentAccess, 10> Accesses{ {
            { typeid(Animator), Access::Read },
            { typeid(FootIKRig), Access::Read },
            { typeid(BoneSkinReference), Access::Read },
            { typeid(Bone), Access::Read },
            { typeid(BoundingBox), Access::Read },
            { typeid(Game::Name), Access::Read },
            { typeid(EntityHierarchy), Access::Read },
            { typeid(TerrainCollidee), Access::Read },
            { typeid(Transform), Access::Write },
            { typeid(FootIKRuntime), Access::Write }
        } };
        return Accesses;
    }

    std::span<const ResourceAccess> FootIKSystem::ResourceAccesses() const {
        static ::std::array<ResourceAccess, 0> Accesses{};
        return Accesses;
    }

    void FootIKSystem::Execute(Arche::World& World, FrameContext& Ctx, const float Dt) {
        const bool IsDebugGeometryDrawEnabled{ (Ctx.RenderData.globals.flags & RFD::FrameGlobalFlagDrawDebugGeometry) != 0u };

        const Arche::World::WorldReadOnlyView& ReadOnlyWorld{ ::std::as_const(World).GetReadOnlyView() };
        ::std::vector<Arche::EntityID> RuntimeMissingEntityIds{};
        RuntimeMissingEntityIds.reserve(32);
        for (auto [AnimatorComponent, FootIKRigComponent, BoneSkinReferenceComponent, TransformComponent, EntityHierarchyComponent] : World.Query<Animator, FootIKRig, BoneSkinReference, Transform, EntityHierarchy>()) {
            (void)AnimatorComponent;
            (void)FootIKRigComponent;
            (void)BoneSkinReferenceComponent;
            (void)TransformComponent;
            if (ReadOnlyWorld.GetComponent<FootIKRuntime>(EntityHierarchyComponent.self) == nullptr) {
                RuntimeMissingEntityIds.push_back(EntityHierarchyComponent.self);
            }
        }

        for (const Arche::EntityID RuntimeMissingEntityId : RuntimeMissingEntityIds) {
            if (World.GetComponent<FootIKRuntime>(RuntimeMissingEntityId) != nullptr) {
                continue;
            }

            FootIKRuntime NewFootIKRuntime{};
            World.AddComponent(RuntimeMissingEntityId, NewFootIKRuntime);
        }

        ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix> WorldMatrices{};
        WorldMatrices.reserve(1024);

        for (auto [AnimatorComponent, FootIKRigComponent, BoneSkinReferenceComponent, TransformComponent, FootIKRuntimeComponent, EntityHierarchyComponent] : World.Query<Animator, FootIKRig, BoneSkinReference, Transform, FootIKRuntime, EntityHierarchy>()) {
            (void)AnimatorComponent;
            (void)TransformComponent;
            (void)EntityHierarchyComponent;

            const ::std::string_view LeftFootBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mLeftFootBoneName) };
            const ::std::string_view RightFootBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mRightFootBoneName) };
            const ::std::string_view LeftToeBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mLeftToeBoneName) };
            const ::std::string_view RightToeBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mRightToeBoneName) };
            const ::std::string_view LeftShinBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mLeftShinBoneName) };
            const ::std::string_view RightShinBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mRightShinBoneName) };
            const ::std::string_view LeftThighBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mLeftThighBoneName) };
            const ::std::string_view RightThighBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mRightThighBoneName) };
            const ::std::string_view PelvisBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mPelvisBoneName) };

            const bool IsLeftFootCachedEntityValid{ IK::IsCachedBoneEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mLeftFootEntityId, LeftFootBoneNameText) };
            const bool IsRightFootCachedEntityValid{ IK::IsCachedBoneEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mRightFootEntityId, RightFootBoneNameText) };
            const bool IsLeftToeCachedEntityValid{ IK::IsCachedBoneEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mLeftToeEntityId, LeftToeBoneNameText) };
            const bool IsRightToeCachedEntityValid{ IK::IsCachedBoneEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mRightToeEntityId, RightToeBoneNameText) };
            const bool IsLeftShinCachedEntityValid{ IK::IsCachedBoneEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mLeftShinEntityId, LeftShinBoneNameText) };
            const bool IsRightShinCachedEntityValid{ IK::IsCachedBoneEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mRightShinEntityId, RightShinBoneNameText) };
            const bool IsLeftThighCachedEntityValid{ IK::IsCachedBoneEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mLeftThighEntityId, LeftThighBoneNameText) };
            const bool IsRightThighCachedEntityValid{ IK::IsCachedBoneEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mRightThighEntityId, RightThighBoneNameText) };
            const bool IsPelvisCachedEntityValid{ IK::IsCachedBoneEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mPelvisEntityId, PelvisBoneNameText) };
            if (IsLeftFootCachedEntityValid == false || IsRightFootCachedEntityValid == false || IsLeftToeCachedEntityValid == false || IsRightToeCachedEntityValid == false || IsLeftShinCachedEntityValid == false || IsRightShinCachedEntityValid == false || IsLeftThighCachedEntityValid == false || IsRightThighCachedEntityValid == false || IsPelvisCachedEntityValid == false) {
                FootIKRuntimeComponent.mResolved = false;
            }

            if (FootIKRuntimeComponent.mResolved == false) {
                IK::ResolveFootBoneEntities(ReadOnlyWorld, FootIKRigComponent, BoneSkinReferenceComponent.boneRootEntityId, FootIKRuntimeComponent);
            }

            float LeftRawTargetOffset{};
            float RightRawTargetOffset{};
            float LeftTargetPlantWeight{};
            float RightTargetPlantWeight{};
            bool IsLeftGroundNormalResolved{};
            bool IsRightGroundNormalResolved{};
            SimpleMath::Vector3 LeftGroundNormal{ SimpleMath::Vector3::Up };
            SimpleMath::Vector3 RightGroundNormal{ SimpleMath::Vector3::Up };
            if (FootIKRigComponent.mEnabled == true && FootIKRuntimeComponent.mResolved == true) {
                float LeftResolvedTargetOffset{};
                SimpleMath::Vector3 LeftResolvedGroundNormal{ SimpleMath::Vector3::Up };
                SimpleMath::Vector3 LeftResolvedGroundSamplePosition{};
                const bool IsLeftTargetOffsetResolved{ IK::TryResolveFootTargetOffset(World, FootIKRuntimeComponent.mLeftFootEntityId, WorldMatrices, LeftResolvedTargetOffset, LeftResolvedGroundNormal, LeftResolvedGroundSamplePosition) };
                if (IsLeftTargetOffsetResolved == true) {
                    LeftRawTargetOffset = LeftResolvedTargetOffset;
                    LeftTargetPlantWeight = IK::ResolveFootPlantWeight(LeftResolvedTargetOffset);
                    LeftGroundNormal = LeftResolvedGroundNormal;
                    IsLeftGroundNormalResolved = true;
                    if (IsDebugGeometryDrawEnabled == true) {
                        AppendGroundSampleDebugLine(Ctx, LeftResolvedGroundSamplePosition, LeftResolvedGroundNormal);
                    }
                }

                float RightResolvedTargetOffset{};
                SimpleMath::Vector3 RightResolvedGroundNormal{ SimpleMath::Vector3::Up };
                SimpleMath::Vector3 RightResolvedGroundSamplePosition{};
                const bool IsRightTargetOffsetResolved{ IK::TryResolveFootTargetOffset(World, FootIKRuntimeComponent.mRightFootEntityId, WorldMatrices, RightResolvedTargetOffset, RightResolvedGroundNormal, RightResolvedGroundSamplePosition) };
                if (IsRightTargetOffsetResolved == true) {
                    RightRawTargetOffset = RightResolvedTargetOffset;
                    RightTargetPlantWeight = IK::ResolveFootPlantWeight(RightResolvedTargetOffset);
                    RightGroundNormal = RightResolvedGroundNormal;
                    IsRightGroundNormalResolved = true;
                    if (IsDebugGeometryDrawEnabled == true) {
                        AppendGroundSampleDebugLine(Ctx, RightResolvedGroundSamplePosition, RightResolvedGroundNormal);
                    }
                }
            }

            const float PreviousLeftPlantWeight{ IsFiniteFloat(FootIKRuntimeComponent.mLeftPlantWeight) ? ::std::clamp(FootIKRuntimeComponent.mLeftPlantWeight, 0.0f, 1.0f) : 0.0f };
            const float PreviousRightPlantWeight{ IsFiniteFloat(FootIKRuntimeComponent.mRightPlantWeight) ? ::std::clamp(FootIKRuntimeComponent.mRightPlantWeight, 0.0f, 1.0f) : 0.0f };
            const float SmoothedLeftPlantWeight{ IK::ResolveSmoothedOffset(PreviousLeftPlantWeight, LeftTargetPlantWeight, FootIKRigComponent.mBlendSpeed, Dt) };
            const float SmoothedRightPlantWeight{ IK::ResolveSmoothedOffset(PreviousRightPlantWeight, RightTargetPlantWeight, FootIKRigComponent.mBlendSpeed, Dt) };
            FootIKRuntimeComponent.mLeftPlantWeight = IsFiniteFloat(SmoothedLeftPlantWeight) ? ::std::clamp(SmoothedLeftPlantWeight, 0.0f, 1.0f) : 0.0f;
            FootIKRuntimeComponent.mRightPlantWeight = IsFiniteFloat(SmoothedRightPlantWeight) ? ::std::clamp(SmoothedRightPlantWeight, 0.0f, 1.0f) : 0.0f;

            const float MaxLift{ (::std::max)(FootIKRigComponent.mMaxLift, 0.0f) };
            const float MaxDrop{ (::std::max)(FootIKRigComponent.mMaxDrop, 0.0f) };
            float LeftTargetOffset{ ::std::clamp(LeftRawTargetOffset, -MaxDrop, MaxLift) };
            float RightTargetOffset{ ::std::clamp(RightRawTargetOffset, -MaxDrop, MaxLift) };
            LeftTargetOffset *= FootIKRuntimeComponent.mLeftPlantWeight;
            RightTargetOffset *= FootIKRuntimeComponent.mRightPlantWeight;

            const float PreviousLeftOffset{ IsFiniteFloat(FootIKRuntimeComponent.mLeftCurrentOffset) ? FootIKRuntimeComponent.mLeftCurrentOffset : 0.0f };
            const float PreviousRightOffset{ IsFiniteFloat(FootIKRuntimeComponent.mRightCurrentOffset) ? FootIKRuntimeComponent.mRightCurrentOffset : 0.0f };
            const float SmoothedLeftOffset{ IK::ResolveSmoothedOffset(PreviousLeftOffset, LeftTargetOffset, FootIKRigComponent.mBlendSpeed, Dt) };
            const float SmoothedRightOffset{ IK::ResolveSmoothedOffset(PreviousRightOffset, RightTargetOffset, FootIKRigComponent.mBlendSpeed, Dt) };
            FootIKRuntimeComponent.mLeftCurrentOffset = IsFiniteFloat(SmoothedLeftOffset) ? SmoothedLeftOffset : 0.0f;
            FootIKRuntimeComponent.mRightCurrentOffset = IsFiniteFloat(SmoothedRightOffset) ? SmoothedRightOffset : 0.0f;
            FootIKRuntimeComponent.mCurrentOffset = IK::ResolveDominantOffset(FootIKRuntimeComponent.mLeftCurrentOffset, FootIKRuntimeComponent.mRightCurrentOffset);

            const float PelvisOffset{ FootIKRuntimeComponent.mPelvisEntityId == Arche::NullEntityID ? 0.0f : IK::ResolveSharedPelvisOffset(FootIKRuntimeComponent.mLeftCurrentOffset, FootIKRuntimeComponent.mRightCurrentOffset) };
            if (FootIKRuntimeComponent.mPelvisEntityId != Arche::NullEntityID) {
                const bool IsPelvisOffsetApplied{ IK::TryApplyOffsetToBoneTransform(World, FootIKRuntimeComponent.mPelvisEntityId, PelvisOffset, WorldMatrices) };
                if (IsPelvisOffsetApplied == true) {
                    WorldMatrices.clear();
                }
            }

            const float LeftLegOffset{ FootIKRuntimeComponent.mLeftCurrentOffset - PelvisOffset };
            const float RightLegOffset{ FootIKRuntimeComponent.mRightCurrentOffset - PelvisOffset };
            if (FootIKRuntimeComponent.mResolved == true && mFootIKSolver != nullptr) {
                const bool IsLeftLegSolved{ IK::TrySolveLegWithIK(World, FootIKRuntimeComponent.mLeftThighEntityId, FootIKRuntimeComponent.mLeftShinEntityId, FootIKRuntimeComponent.mLeftFootEntityId, LeftLegOffset, *mFootIKSolver, WorldMatrices) };
                const bool IsRightLegSolved{ IK::TrySolveLegWithIK(World, FootIKRuntimeComponent.mRightThighEntityId, FootIKRuntimeComponent.mRightShinEntityId, FootIKRuntimeComponent.mRightFootEntityId, RightLegOffset, *mFootIKSolver, WorldMatrices) };
                if (IsLeftLegSolved == true || IsRightLegSolved == true) {
                    WorldMatrices.clear();
                }
            }

            if (FootIKRigComponent.mEnabled == true && FootIKRuntimeComponent.mResolved == true) {
                if (IsLeftGroundNormalResolved == true) {
                    const bool IsLeftFootSurfaceAligned{ IK::TryAlignFootToSurface(World, FootIKRuntimeComponent.mLeftFootEntityId, FootIKRuntimeComponent.mLeftToeEntityId, LeftGroundNormal, FootIKRuntimeComponent.mLeftPlantWeight, WorldMatrices) };
                    if (IsLeftFootSurfaceAligned == true) {
                        WorldMatrices.clear();
                    }
                }

                if (IsRightGroundNormalResolved == true) {
                    const bool IsRightFootSurfaceAligned{ IK::TryAlignFootToSurface(World, FootIKRuntimeComponent.mRightFootEntityId, FootIKRuntimeComponent.mRightToeEntityId, RightGroundNormal, FootIKRuntimeComponent.mRightPlantWeight, WorldMatrices) };
                    if (IsRightFootSurfaceAligned == true) {
                        WorldMatrices.clear();
                    }
                }
            }
        }
    }
}
