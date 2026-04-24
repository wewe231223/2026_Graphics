#include "Game/Scene/Systems/FootIKSystem.h"

#include <algorithm>
#include <array>
#include <cstddef>
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
#include "Game/Scene/Components/PhysicsActor.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/IK/FabrikFootIKSolver.h"
#include "Game/Scene/IK/FootIKAlgorithms.h"
#include "Game/Scene/IK/FootIKSolver.h"
#include "PhysicsLib/Actors/PhysicsTerrainActor.h"

namespace {
    constexpr std::size_t MinimumRayHitCountForFootSurfaceAlignment{ 3 };

    bool IsFiniteFloat(const float Value) {
        return std::isfinite(Value) != 0;
    }

    bool IsFiniteVector3(const SimpleMath::Vector3& Value) {
        return IsFiniteFloat(Value.x) && IsFiniteFloat(Value.y) && IsFiniteFloat(Value.z);
    }

    bool TryResolveRaycastHitOnTerrain(Arche::World& World, const SimpleMath::Ray& Ray, const float RayLength, SimpleMath::Vector3& OutHitPoint, SimpleMath::Vector3& OutHitNormal) {
        if (IsFiniteVector3(Ray.position) == false || IsFiniteVector3(Ray.direction) == false || IsFiniteFloat(RayLength) == false || RayLength <= 0.0f) {
            return false;
        }

        bool IsHit{};
        float NearestHitDistance{ RayLength };
        SimpleMath::Vector3 NearestHitPoint{};
        SimpleMath::Vector3 NearestHitNormal{ SimpleMath::Vector3::Up };
        for (const auto [PhysicsActorComponent] : World.Query<Game::PhysicsActor>()) {
            const PhysicsActorBase* ActorPointer{ PhysicsActorComponent.mActorPointer };
            const PhysicsTerrainActor* TerrainActorPointer{ dynamic_cast<const PhysicsTerrainActor*>(ActorPointer) };
            if (TerrainActorPointer == nullptr) {
                continue;
            }

            SimpleMath::Vector3 CandidateHitPoint{};
            SimpleMath::Vector3 CandidateHitNormal{ SimpleMath::Vector3::Up };
            float CandidateHitDistance{};
            const bool IsCandidateHit{ TerrainActorPointer->TryRaycast(Ray, RayLength, CandidateHitPoint, CandidateHitNormal, CandidateHitDistance) };
            if (IsCandidateHit == false || IsFiniteVector3(CandidateHitPoint) == false || IsFiniteVector3(CandidateHitNormal) == false || IsFiniteFloat(CandidateHitDistance) == false || CandidateHitDistance < 0.0f || CandidateHitDistance > RayLength) {
                continue;
            }

            if (IsHit == false || CandidateHitDistance < NearestHitDistance) {
                IsHit = true;
                NearestHitDistance = CandidateHitDistance;
                NearestHitPoint = CandidateHitPoint;
                NearestHitNormal = CandidateHitNormal;
            }
        }

        if (IsHit == false) {
            return false;
        }

        OutHitPoint = NearestHitPoint;
        OutHitNormal = NearestHitNormal;
        return true;
    }

    void AppendFootCornerDebugLines(Arche::World& World, Game::FrameContext& Ctx, const Arche::EntityID FootEntityId, const Arche::EntityID ToeEntityId, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        constexpr float RayStartOffset{ 0.3f };
        constexpr float RayLineLength{ 0.3f + 0.2f };
        constexpr float HitNormalLineLength{ 0.12f };
        constexpr float CornerLineThickness{ 0.0025f };
        constexpr SimpleMath::Vector4 CornerLineColor{ 0.35f, 1.0f, 0.45f, 1.0f };
        constexpr SimpleMath::Vector4 HitLineColor{ 1.0f, 0.15f, 0.15f, 1.0f };
        constexpr SimpleMath::Vector4 HitNormalLineColor{ 0.15f, 0.65f, 1.0f, 1.0f };

        std::array<SimpleMath::Vector3, 4> CornerPoints{};
        std::array<SimpleMath::Vector3, 4> CornerDirections{};
        const bool IsCornerPointsResolved{ Game::IK::TryResolveFootObbAndToeObbCorners(World, FootEntityId, ToeEntityId, InOutWorldMatrices, CornerPoints, CornerDirections) };
        if (IsCornerPointsResolved == false) {
            return;
        }

        for (std::size_t CornerIndex{}; CornerIndex < CornerPoints.size(); ++CornerIndex) {
            const SimpleMath::Vector3& CornerPoint{ CornerPoints[CornerIndex] };
            const SimpleMath::Vector3& RayDirection{ CornerDirections[CornerIndex] };
            const SimpleMath::Vector3 RayStartPoint{ CornerPoint - (RayDirection * RayStartOffset) };
            if (IsFiniteVector3(RayStartPoint) == false) {
                continue;
            }

            const SimpleMath::Ray Ray{ RayStartPoint, RayDirection };
            SimpleMath::Vector3 HitPoint{};
            SimpleMath::Vector3 HitNormal{ SimpleMath::Vector3::Up };
            const bool IsTerrainHit{ TryResolveRaycastHitOnTerrain(World, Ray, RayLineLength, HitPoint, HitNormal) };
            if (IsTerrainHit == true && HitNormal.Dot(RayDirection) > 0.0f) {
                HitNormal *= -1.0f;
            }

            const SimpleMath::Vector4& LineColor{ IsTerrainHit == true ? HitLineColor : CornerLineColor };
            Ctx.RenderData.debugGeometryContexts.push_back(Game::RFD::DebugGeometryContext::CreateDirection(RayStartPoint, RayDirection, RayLineLength, LineColor, CornerLineThickness));
            if (IsTerrainHit == true) {
                Ctx.RenderData.debugGeometryContexts.push_back(Game::RFD::DebugGeometryContext::CreateDirection(HitPoint, HitNormal, HitNormalLineLength, HitNormalLineColor, CornerLineThickness));
            }
        }
    }

    struct FootIKSolveTarget final {
        bool IsTargetResolved{};
        float RawTargetOffset{};
        bool IsGroundNormalResolved{};
        SimpleMath::Vector3 RayOppositeDirection{ SimpleMath::Vector3::Up };
        SimpleMath::Vector3 GroundNormal{ SimpleMath::Vector3::Up };
        SimpleMath::Vector3 TargetFootWorldPosition{};
    };
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
        : mName{ std::move(Other.mName) },
          mFootIKSolver{ std::move(Other.mFootIKSolver) } {
        if (mFootIKSolver == nullptr) {
            mFootIKSolver = CreateFabrikFootIKSolver();
        }
    }

    FootIKSystem& FootIKSystem::operator=(FootIKSystem&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mName = std::move(Other.mName);
        mFootIKSolver = std::move(Other.mFootIKSolver);
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
        static std::array<ComponentAccess, 10> Accesses{ {
            { typeid(Animator), Access::Read },
            { typeid(FootIKRig), Access::Read },
            { typeid(BoneSkinReference), Access::Read },
            { typeid(Bone), Access::Read },
            { typeid(BoundingBox), Access::Read },
            { typeid(Game::Name), Access::Read },
            { typeid(EntityHierarchy), Access::Read },
            { typeid(PhysicsActor), Access::Read },
            { typeid(Transform), Access::Write },
            { typeid(FootIKRuntime), Access::Write }
        } };
        return Accesses;
    }

    std::span<const ResourceAccess> FootIKSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 0> Accesses{};
        return Accesses;
    }

    void FootIKSystem::Execute(Arche::World& World, FrameContext& Ctx, const float Dt) {
        const bool IsDebugGeometryDrawEnabled{ (Ctx.RenderData.globals.flags & RFD::FrameGlobalFlagDrawDebugGeometry) != 0u };

        const Arche::World::WorldReadOnlyView& ReadOnlyWorld{ std::as_const(World).GetReadOnlyView() };
        std::vector<Arche::EntityID> RuntimeMissingEntityIds{};
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

        std::unordered_map<Arche::EntityID, SimpleMath::Matrix> WorldMatrices{};
        WorldMatrices.reserve(1024);

        for (auto [AnimatorComponent, FootIKRigComponent, BoneSkinReferenceComponent, TransformComponent, FootIKRuntimeComponent, EntityHierarchyComponent] : World.Query<Animator, FootIKRig, BoneSkinReference, Transform, FootIKRuntime, EntityHierarchy>()) {
            (void)AnimatorComponent;
            (void)TransformComponent;
            (void)EntityHierarchyComponent;

            const std::string_view LeftFootBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mLeftFootBoneName) };
            const std::string_view RightFootBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mRightFootBoneName) };
            const std::string_view LeftToeBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mLeftToeBoneName) };
            const std::string_view RightToeBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mRightToeBoneName) };
            const std::string_view LeftShinBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mLeftShinBoneName) };
            const std::string_view RightShinBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mRightShinBoneName) };
            const std::string_view LeftThighBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mLeftThighBoneName) };
            const std::string_view RightThighBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mRightThighBoneName) };
            const std::string_view PelvisBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mPelvisBoneName) };

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

            FootIKSolveTarget LeftSolveTarget{};
            FootIKSolveTarget RightSolveTarget{};
            if (FootIKRigComponent.mEnabled == true && FootIKRuntimeComponent.mResolved == true) {
                float LeftResolvedTargetOffset{};
                SimpleMath::Vector3 LeftResolvedRayOppositeDirection{ SimpleMath::Vector3::Up };
                SimpleMath::Vector3 LeftResolvedGroundNormal{ SimpleMath::Vector3::Up };
                SimpleMath::Vector3 LeftResolvedTargetFootWorldPosition{};
                std::size_t LeftResolvedHitCount{};
                const bool IsLeftTargetOffsetResolved{ IK::TryResolveFootTargetOffset(World, FootIKRuntimeComponent.mLeftFootEntityId, FootIKRuntimeComponent.mLeftToeEntityId, WorldMatrices, LeftResolvedTargetOffset, LeftResolvedRayOppositeDirection, LeftResolvedGroundNormal, LeftResolvedTargetFootWorldPosition, LeftResolvedHitCount) };
                if (IsLeftTargetOffsetResolved == true && LeftResolvedHitCount >= MinimumRayHitCountForFootSurfaceAlignment) {
                    LeftSolveTarget.IsTargetResolved = true;
                    LeftSolveTarget.RawTargetOffset = LeftResolvedTargetOffset;
                    LeftSolveTarget.IsGroundNormalResolved = true;
                    LeftSolveTarget.RayOppositeDirection = LeftResolvedRayOppositeDirection;
                    LeftSolveTarget.GroundNormal = LeftResolvedGroundNormal;
                    LeftSolveTarget.TargetFootWorldPosition = LeftResolvedTargetFootWorldPosition;
                }

                float RightResolvedTargetOffset{};
                SimpleMath::Vector3 RightResolvedRayOppositeDirection{ SimpleMath::Vector3::Up };
                SimpleMath::Vector3 RightResolvedGroundNormal{ SimpleMath::Vector3::Up };
                SimpleMath::Vector3 RightResolvedTargetFootWorldPosition{};
                std::size_t RightResolvedHitCount{};
                const bool IsRightTargetOffsetResolved{ IK::TryResolveFootTargetOffset(World, FootIKRuntimeComponent.mRightFootEntityId, FootIKRuntimeComponent.mRightToeEntityId, WorldMatrices, RightResolvedTargetOffset, RightResolvedRayOppositeDirection, RightResolvedGroundNormal, RightResolvedTargetFootWorldPosition, RightResolvedHitCount) };
                if (IsRightTargetOffsetResolved == true && RightResolvedHitCount >= MinimumRayHitCountForFootSurfaceAlignment) {
                    RightSolveTarget.IsTargetResolved = true;
                    RightSolveTarget.RawTargetOffset = RightResolvedTargetOffset;
                    RightSolveTarget.IsGroundNormalResolved = true;
                    RightSolveTarget.RayOppositeDirection = RightResolvedRayOppositeDirection;
                    RightSolveTarget.GroundNormal = RightResolvedGroundNormal;
                    RightSolveTarget.TargetFootWorldPosition = RightResolvedTargetFootWorldPosition;
                }
            }

            const float MaxLift{ (std::max)(FootIKRigComponent.mMaxLift, 0.0f) };
            const float MaxDrop{ (std::max)(FootIKRigComponent.mMaxDrop, 0.0f) };
            const float LeftTargetOffset{ LeftSolveTarget.IsTargetResolved == true ? std::clamp(LeftSolveTarget.RawTargetOffset, -MaxDrop, MaxLift) : 0.0f };
            const float RightTargetOffset{ RightSolveTarget.IsTargetResolved == true ? std::clamp(RightSolveTarget.RawTargetOffset, -MaxDrop, MaxLift) : 0.0f };

            const float PreviousLeftOffset{ IsFiniteFloat(FootIKRuntimeComponent.mLeftCurrentOffset) ? FootIKRuntimeComponent.mLeftCurrentOffset : 0.0f };
            const float PreviousRightOffset{ IsFiniteFloat(FootIKRuntimeComponent.mRightCurrentOffset) ? FootIKRuntimeComponent.mRightCurrentOffset : 0.0f };
            const float SmoothedLeftOffset{ IK::ResolveSmoothedOffset(PreviousLeftOffset, LeftTargetOffset, FootIKRigComponent.mBlendSpeed, Dt) };
            const float SmoothedRightOffset{ IK::ResolveSmoothedOffset(PreviousRightOffset, RightTargetOffset, FootIKRigComponent.mBlendSpeed, Dt) };
            FootIKRuntimeComponent.mLeftCurrentOffset = IsFiniteFloat(SmoothedLeftOffset) ? SmoothedLeftOffset : 0.0f;
            FootIKRuntimeComponent.mRightCurrentOffset = IsFiniteFloat(SmoothedRightOffset) ? SmoothedRightOffset : 0.0f;

            SimpleMath::Vector3 LeftTargetFootWorldPosition{};
            bool IsLeftTargetFootWorldPositionResolved{};
            if (LeftSolveTarget.IsTargetResolved == true) {
                const float LeftBaseFootWorldPositionY{ LeftSolveTarget.TargetFootWorldPosition.y - LeftSolveTarget.RawTargetOffset };
                LeftTargetFootWorldPosition = LeftSolveTarget.TargetFootWorldPosition;
                LeftTargetFootWorldPosition.y = LeftBaseFootWorldPositionY + FootIKRuntimeComponent.mLeftCurrentOffset;
                IsLeftTargetFootWorldPositionResolved = IsFiniteVector3(LeftTargetFootWorldPosition);
            }

            SimpleMath::Vector3 RightTargetFootWorldPosition{};
            bool IsRightTargetFootWorldPositionResolved{};
            if (RightSolveTarget.IsTargetResolved == true) {
                const float RightBaseFootWorldPositionY{ RightSolveTarget.TargetFootWorldPosition.y - RightSolveTarget.RawTargetOffset };
                RightTargetFootWorldPosition = RightSolveTarget.TargetFootWorldPosition;
                RightTargetFootWorldPosition.y = RightBaseFootWorldPositionY + FootIKRuntimeComponent.mRightCurrentOffset;
                IsRightTargetFootWorldPositionResolved = IsFiniteVector3(RightTargetFootWorldPosition);
            }

            const float LeftCurrentOffsetForPelvis{ LeftSolveTarget.IsTargetResolved == true ? FootIKRuntimeComponent.mLeftCurrentOffset : 0.0f };
            const float RightCurrentOffsetForPelvis{ RightSolveTarget.IsTargetResolved == true ? FootIKRuntimeComponent.mRightCurrentOffset : 0.0f };
            float PelvisOffset{ FootIKRuntimeComponent.mPelvisEntityId == Arche::NullEntityID ? 0.0f : IK::ResolveSharedPelvisOffset(LeftCurrentOffsetForPelvis, RightCurrentOffsetForPelvis) };
            if (FootIKRuntimeComponent.mPelvisEntityId != Arche::NullEntityID) {
                float LeftReachOverflowDistance{};
                if (IsLeftTargetFootWorldPositionResolved == true) {
                    float ResolvedLeftReachOverflowDistance{};
                    if (IK::TryResolveLegReachOverflowDistance(World, FootIKRuntimeComponent.mLeftThighEntityId, FootIKRuntimeComponent.mLeftShinEntityId, FootIKRuntimeComponent.mLeftFootEntityId, LeftTargetFootWorldPosition, WorldMatrices, ResolvedLeftReachOverflowDistance) == true) {
                        LeftReachOverflowDistance = ResolvedLeftReachOverflowDistance;
                    }
                }

                float RightReachOverflowDistance{};
                if (IsRightTargetFootWorldPositionResolved == true) {
                    float ResolvedRightReachOverflowDistance{};
                    if (IK::TryResolveLegReachOverflowDistance(World, FootIKRuntimeComponent.mRightThighEntityId, FootIKRuntimeComponent.mRightShinEntityId, FootIKRuntimeComponent.mRightFootEntityId, RightTargetFootWorldPosition, WorldMatrices, ResolvedRightReachOverflowDistance) == true) {
                        RightReachOverflowDistance = ResolvedRightReachOverflowDistance;
                    }
                }

                const float MaximumReachOverflowDistance{ (std::max)(LeftReachOverflowDistance, RightReachOverflowDistance) };
                if (IsFiniteFloat(MaximumReachOverflowDistance) == true && MaximumReachOverflowDistance > 0.0f) {
                    PelvisOffset -= MaximumReachOverflowDistance;
                }

                const bool IsPelvisOffsetApplied{ IK::TryApplyOffsetToBoneTransform(World, FootIKRuntimeComponent.mPelvisEntityId, PelvisOffset, WorldMatrices) };
                if (IsPelvisOffsetApplied == true) {
                    WorldMatrices.clear();
                }
            }

            if (FootIKRigComponent.mEnabled == true && FootIKRuntimeComponent.mResolved == true) {
                if (mFootIKSolver != nullptr) {
                    if (IsLeftTargetFootWorldPositionResolved == true) {
                        const bool IsLeftLegSolved{ IK::TrySolveLegWithIK(World, FootIKRuntimeComponent.mLeftThighEntityId, FootIKRuntimeComponent.mLeftShinEntityId, FootIKRuntimeComponent.mLeftFootEntityId, LeftTargetFootWorldPosition, *mFootIKSolver, WorldMatrices) };
                        if (IsLeftLegSolved == true) {
                            WorldMatrices.clear();
                        }
                    }

                    if (IsRightTargetFootWorldPositionResolved == true) {
                        const bool IsRightLegSolved{ IK::TrySolveLegWithIK(World, FootIKRuntimeComponent.mRightThighEntityId, FootIKRuntimeComponent.mRightShinEntityId, FootIKRuntimeComponent.mRightFootEntityId, RightTargetFootWorldPosition, *mFootIKSolver, WorldMatrices) };
                        if (IsRightLegSolved == true) {
                            WorldMatrices.clear();
                        }
                    }
                }

                if (LeftSolveTarget.IsGroundNormalResolved == true) {
                    const bool IsLeftFootSurfaceAligned{ IK::TryAlignFootToSurface(World, FootIKRuntimeComponent.mLeftFootEntityId, FootIKRuntimeComponent.mLeftToeEntityId, LeftSolveTarget.RayOppositeDirection, LeftSolveTarget.GroundNormal, 1.0f, WorldMatrices) };
                    if (IsLeftFootSurfaceAligned == true) {
                        WorldMatrices.clear();
                    }
                }

                if (RightSolveTarget.IsGroundNormalResolved == true) {
                    const bool IsRightFootSurfaceAligned{ IK::TryAlignFootToSurface(World, FootIKRuntimeComponent.mRightFootEntityId, FootIKRuntimeComponent.mRightToeEntityId, RightSolveTarget.RayOppositeDirection, RightSolveTarget.GroundNormal, 1.0f, WorldMatrices) };
                    if (IsRightFootSurfaceAligned == true) {
                        WorldMatrices.clear();
                    }
                }

                if (IsDebugGeometryDrawEnabled == true) {
                    AppendFootCornerDebugLines(World, Ctx, FootIKRuntimeComponent.mLeftFootEntityId, FootIKRuntimeComponent.mLeftToeEntityId, WorldMatrices);
                    AppendFootCornerDebugLines(World, Ctx, FootIKRuntimeComponent.mRightFootEntityId, FootIKRuntimeComponent.mRightToeEntityId, WorldMatrices);
                }
            }
        }
    }
}
