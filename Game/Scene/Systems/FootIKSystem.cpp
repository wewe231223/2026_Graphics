#include "FootIKSystem.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/FootIKRig.h"
#include "Game/Scene/Components/FootIKRuntime.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/IK/FabrikFootIKSolver.h"
#include "Game/Scene/IK/FootIKAlgorithms.h"
#include "Game/Scene/Base/Context.h"
#include "Game/Terrain/TerrainQuery.h"
#include "Utility/MathValidation.h"

namespace Game {
    namespace Pipeline {
        namespace {
            constexpr std::size_t MinimumRayHitCountForFootSurfaceAlignment{ 3ULL };

            struct FootIKSolveTarget final {
            public:
                bool mIsTargetResolved{};
                float mRawTargetOffset{};
                bool mIsGroundNormalResolved{};
                SimpleMath::Vector3 mRayOppositeDirection{ SimpleMath::Vector3::Up };
                SimpleMath::Vector3 mGroundNormal{ SimpleMath::Vector3::Up };
                SimpleMath::Vector3 mTargetFootWorldPosition{};
            };

        }

        bool PipelineFootIKSystem::TryResolveRaycastHitOnTerrain(const ITerrainQuery& TerrainQuery, const SimpleMath::Ray& Ray, float RayLength, SimpleMath::Vector3& OutHitPoint, SimpleMath::Vector3& OutHitNormal) const {
            if (MathUtility::IsFiniteVector3(Ray.position) == false || MathUtility::IsFiniteVector3(Ray.direction) == false || MathUtility::IsFiniteFloat(RayLength) == false || RayLength <= 0.0f) {
                return false;
            }

            float HitDistance{};
            const bool IsHit{ TerrainQuery.TryRaycast(Ray, RayLength, OutHitPoint, OutHitNormal, HitDistance) };
            if (IsHit == false || MathUtility::IsFiniteVector3(OutHitPoint) == false || MathUtility::IsFiniteVector3(OutHitNormal) == false || MathUtility::IsFiniteFloat(HitDistance) == false || HitDistance < 0.0f || HitDistance > RayLength) {
                return false;
            }

            return true;
        }

        void PipelineFootIKSystem::AppendFootCornerDebugLines(PipelineContext& Ctx, const ITerrainQuery& TerrainQuery, Arche::EntityID FootEntityId, Arche::EntityID ToeEntityId, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) const {
            if (Ctx.HasRenderFlag(RFD::FrameGlobalFlagDrawDebugGeometry) == false) {
                return;
            }

            constexpr float RayStartOffset{ 0.3f };
            constexpr float RayLineLength{ 0.3f + 0.2f };
            constexpr float HitNormalLineLength{ 0.12f };
            constexpr float CornerLineThickness{ 0.0025f };
            constexpr SimpleMath::Vector4 CornerLineColor{ 0.35f, 1.0f, 0.45f, 1.0f };
            constexpr SimpleMath::Vector4 HitLineColor{ 1.0f, 0.15f, 0.15f, 1.0f };
            constexpr SimpleMath::Vector4 HitNormalLineColor{ 0.15f, 0.65f, 1.0f, 1.0f };

            std::array<SimpleMath::Vector3, 4ULL> CornerPoints{};
            std::array<SimpleMath::Vector3, 4ULL> CornerDirections{};
            const bool IsCornerPointsResolved{ IK::TryResolveFootObbAndToeObbCorners(Ctx.GetWorld(), FootEntityId, ToeEntityId, InOutWorldMatrices, CornerPoints, CornerDirections) };
            if (IsCornerPointsResolved == false) {
                return;
            }

            for (std::size_t CornerIndex{}; CornerIndex < CornerPoints.size(); ++CornerIndex) {
                const SimpleMath::Vector3& CornerPoint{ CornerPoints[CornerIndex] };
                const SimpleMath::Vector3& RayDirection{ CornerDirections[CornerIndex] };
                const SimpleMath::Vector3 RayStartPoint{ CornerPoint - (RayDirection * RayStartOffset) };
                if (MathUtility::IsFiniteVector3(RayStartPoint) == false) {
                    continue;
                }

                const SimpleMath::Ray Ray{ RayStartPoint, RayDirection };
                SimpleMath::Vector3 HitPoint{};
                SimpleMath::Vector3 HitNormal{ SimpleMath::Vector3::Up };
                const bool IsTerrainHit{ TryResolveRaycastHitOnTerrain(TerrainQuery, Ray, RayLineLength, HitPoint, HitNormal) };
                if (IsTerrainHit == true && HitNormal.Dot(RayDirection) > 0.0f) {
                    HitNormal *= -1.0f;
                }

                const SimpleMath::Vector4& LineColor{ IsTerrainHit == true ? HitLineColor : CornerLineColor };
                Ctx.GetRenderGatherResult().GetDebugGeometryContexts().push_back(RFD::DebugGeometryContext::CreateDirection(RayStartPoint, RayDirection, RayLineLength, LineColor, CornerLineThickness));
                if (IsTerrainHit == true) {
                    Ctx.GetRenderGatherResult().GetDebugGeometryContexts().push_back(RFD::DebugGeometryContext::CreateDirection(HitPoint, HitNormal, HitNormalLineLength, HitNormalLineColor, CornerLineThickness));
                }
            }
        }

        PipelineFootIKSystem::PipelineFootIKSystem()
            : mFootIKSolver{ CreateFabrikFootIKSolver() } {
        }

        PipelineFootIKSystem::~PipelineFootIKSystem() {
        }

        PipelineFootIKSystem::PipelineFootIKSystem(const PipelineFootIKSystem& Other)
            : mFootIKSolver{ Other.mFootIKSolver == nullptr ? CreateFabrikFootIKSolver() : Other.mFootIKSolver->Clone() } {
        }

        PipelineFootIKSystem& PipelineFootIKSystem::operator=(const PipelineFootIKSystem& Other) {
            if (this == &Other) {
                return *this;
            }

            mFootIKSolver = Other.mFootIKSolver == nullptr ? CreateFabrikFootIKSolver() : Other.mFootIKSolver->Clone();
            return *this;
        }

        PipelineFootIKSystem::PipelineFootIKSystem(PipelineFootIKSystem&& Other) noexcept
            : mFootIKSolver{ std::move(Other.mFootIKSolver) } {
            if (mFootIKSolver == nullptr) {
                mFootIKSolver = CreateFabrikFootIKSolver();
            }
        }

        PipelineFootIKSystem& PipelineFootIKSystem::operator=(PipelineFootIKSystem&& Other) noexcept {
            if (this == &Other) {
                return *this;
            }

            mFootIKSolver = std::move(Other.mFootIKSolver);
            if (mFootIKSolver == nullptr) {
                mFootIKSolver = CreateFabrikFootIKSolver();
            }

            return *this;
        }

        const std::string& PipelineFootIKSystem::Name() const {
            static const std::string NameText{ "FootIKSystem" };
            return NameText;
        }

        void PipelineFootIKSystem::Execute(PipelineContext& Ctx, float Dt) {
            const ITerrainQuery* TerrainQueryResource{ Ctx.GetTerrainQuery() };
            Arche::World& World{ Ctx.GetWorld() };
            const Arche::World::WorldReadOnlyView& ReadOnlyWorld{ std::as_const(World).GetReadOnlyView() };

            std::unordered_map<Arche::EntityID, SimpleMath::Matrix> WorldMatrices{};
            WorldMatrices.reserve(1024ULL);

            Ctx.ForEach<Animator, FootIKRig, BoneSkinReference, Transform, FootIKRuntime, EntityHierarchy>([&](Animator& AnimatorComponent, FootIKRig& FootIKRigComponent, BoneSkinReference& BoneSkinReferenceComponent, Transform& TransformComponent, FootIKRuntime& FootIKRuntimeComponent, EntityHierarchy& EntityHierarchyComponent) {
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
                if (FootIKRigComponent.mEnabled == true && FootIKRuntimeComponent.mResolved == true && TerrainQueryResource != nullptr) {
                    float LeftResolvedTargetOffset{};
                    SimpleMath::Vector3 LeftResolvedRayOppositeDirection{ SimpleMath::Vector3::Up };
                    SimpleMath::Vector3 LeftResolvedGroundNormal{ SimpleMath::Vector3::Up };
                    SimpleMath::Vector3 LeftResolvedTargetFootWorldPosition{};
                    std::size_t LeftResolvedHitCount{};
                    const bool IsLeftTargetOffsetResolved{ IK::TryResolveFootTargetOffset(World, *TerrainQueryResource, FootIKRuntimeComponent.mLeftFootEntityId, FootIKRuntimeComponent.mLeftToeEntityId, WorldMatrices, LeftResolvedTargetOffset, LeftResolvedRayOppositeDirection, LeftResolvedGroundNormal, LeftResolvedTargetFootWorldPosition, LeftResolvedHitCount) };
                    if (IsLeftTargetOffsetResolved == true && LeftResolvedHitCount >= MinimumRayHitCountForFootSurfaceAlignment) {
                        LeftSolveTarget.mIsTargetResolved = true;
                        LeftSolveTarget.mRawTargetOffset = LeftResolvedTargetOffset;
                        LeftSolveTarget.mIsGroundNormalResolved = true;
                        LeftSolveTarget.mRayOppositeDirection = LeftResolvedRayOppositeDirection;
                        LeftSolveTarget.mGroundNormal = LeftResolvedGroundNormal;
                        LeftSolveTarget.mTargetFootWorldPosition = LeftResolvedTargetFootWorldPosition;
                    }

                    float RightResolvedTargetOffset{};
                    SimpleMath::Vector3 RightResolvedRayOppositeDirection{ SimpleMath::Vector3::Up };
                    SimpleMath::Vector3 RightResolvedGroundNormal{ SimpleMath::Vector3::Up };
                    SimpleMath::Vector3 RightResolvedTargetFootWorldPosition{};
                    std::size_t RightResolvedHitCount{};
                    const bool IsRightTargetOffsetResolved{ IK::TryResolveFootTargetOffset(World, *TerrainQueryResource, FootIKRuntimeComponent.mRightFootEntityId, FootIKRuntimeComponent.mRightToeEntityId, WorldMatrices, RightResolvedTargetOffset, RightResolvedRayOppositeDirection, RightResolvedGroundNormal, RightResolvedTargetFootWorldPosition, RightResolvedHitCount) };
                    if (IsRightTargetOffsetResolved == true && RightResolvedHitCount >= MinimumRayHitCountForFootSurfaceAlignment) {
                        RightSolveTarget.mIsTargetResolved = true;
                        RightSolveTarget.mRawTargetOffset = RightResolvedTargetOffset;
                        RightSolveTarget.mIsGroundNormalResolved = true;
                        RightSolveTarget.mRayOppositeDirection = RightResolvedRayOppositeDirection;
                        RightSolveTarget.mGroundNormal = RightResolvedGroundNormal;
                        RightSolveTarget.mTargetFootWorldPosition = RightResolvedTargetFootWorldPosition;
                    }
                }

                const float MaxLift{ std::max(FootIKRigComponent.mMaxLift, 0.0f) };
                const float MaxDrop{ std::max(FootIKRigComponent.mMaxDrop, 0.0f) };
                const float LeftTargetOffset{ LeftSolveTarget.mIsTargetResolved == true ? std::clamp(LeftSolveTarget.mRawTargetOffset, -MaxDrop, MaxLift) : 0.0f };
                const float RightTargetOffset{ RightSolveTarget.mIsTargetResolved == true ? std::clamp(RightSolveTarget.mRawTargetOffset, -MaxDrop, MaxLift) : 0.0f };

                const float PreviousLeftOffset{ MathUtility::IsFiniteFloat(FootIKRuntimeComponent.mLeftCurrentOffset) == true ? FootIKRuntimeComponent.mLeftCurrentOffset : 0.0f };
                const float PreviousRightOffset{ MathUtility::IsFiniteFloat(FootIKRuntimeComponent.mRightCurrentOffset) == true ? FootIKRuntimeComponent.mRightCurrentOffset : 0.0f };
                const float SmoothedLeftOffset{ IK::ResolveSmoothedOffset(PreviousLeftOffset, LeftTargetOffset, FootIKRigComponent.mBlendSpeed, Dt) };
                const float SmoothedRightOffset{ IK::ResolveSmoothedOffset(PreviousRightOffset, RightTargetOffset, FootIKRigComponent.mBlendSpeed, Dt) };
                FootIKRuntimeComponent.mLeftCurrentOffset = MathUtility::IsFiniteFloat(SmoothedLeftOffset) == true ? SmoothedLeftOffset : 0.0f;
                FootIKRuntimeComponent.mRightCurrentOffset = MathUtility::IsFiniteFloat(SmoothedRightOffset) == true ? SmoothedRightOffset : 0.0f;

                SimpleMath::Vector3 LeftTargetFootWorldPosition{};
                bool IsLeftTargetFootWorldPositionResolved{};
                if (LeftSolveTarget.mIsTargetResolved == true) {
                    const float LeftBaseFootWorldPositionY{ LeftSolveTarget.mTargetFootWorldPosition.y - LeftSolveTarget.mRawTargetOffset };
                    LeftTargetFootWorldPosition = LeftSolveTarget.mTargetFootWorldPosition;
                    LeftTargetFootWorldPosition.y = LeftBaseFootWorldPositionY + FootIKRuntimeComponent.mLeftCurrentOffset;
                    IsLeftTargetFootWorldPositionResolved = MathUtility::IsFiniteVector3(LeftTargetFootWorldPosition);
                }

                SimpleMath::Vector3 RightTargetFootWorldPosition{};
                bool IsRightTargetFootWorldPositionResolved{};
                if (RightSolveTarget.mIsTargetResolved == true) {
                    const float RightBaseFootWorldPositionY{ RightSolveTarget.mTargetFootWorldPosition.y - RightSolveTarget.mRawTargetOffset };
                    RightTargetFootWorldPosition = RightSolveTarget.mTargetFootWorldPosition;
                    RightTargetFootWorldPosition.y = RightBaseFootWorldPositionY + FootIKRuntimeComponent.mRightCurrentOffset;
                    IsRightTargetFootWorldPositionResolved = MathUtility::IsFiniteVector3(RightTargetFootWorldPosition);
                }

                const float LeftCurrentOffsetForPelvis{ LeftSolveTarget.mIsTargetResolved == true ? FootIKRuntimeComponent.mLeftCurrentOffset : 0.0f };
                const float RightCurrentOffsetForPelvis{ RightSolveTarget.mIsTargetResolved == true ? FootIKRuntimeComponent.mRightCurrentOffset : 0.0f };
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

                    const float MaximumReachOverflowDistance{ std::max(LeftReachOverflowDistance, RightReachOverflowDistance) };
                    if (MathUtility::IsFiniteFloat(MaximumReachOverflowDistance) == true && MaximumReachOverflowDistance > 0.0f) {
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

                    if (LeftSolveTarget.mIsGroundNormalResolved == true && TerrainQueryResource != nullptr) {
                        const bool IsLeftFootSurfaceAligned{ IK::TryAlignFootToSurface(World, *TerrainQueryResource, FootIKRuntimeComponent.mLeftFootEntityId, FootIKRuntimeComponent.mLeftToeEntityId, LeftSolveTarget.mRayOppositeDirection, LeftSolveTarget.mGroundNormal, 1.0f, WorldMatrices) };
                        if (IsLeftFootSurfaceAligned == true) {
                            WorldMatrices.clear();
                        }
                    }

                    if (RightSolveTarget.mIsGroundNormalResolved == true && TerrainQueryResource != nullptr) {
                        const bool IsRightFootSurfaceAligned{ IK::TryAlignFootToSurface(World, *TerrainQueryResource, FootIKRuntimeComponent.mRightFootEntityId, FootIKRuntimeComponent.mRightToeEntityId, RightSolveTarget.mRayOppositeDirection, RightSolveTarget.mGroundNormal, 1.0f, WorldMatrices) };
                        if (IsRightFootSurfaceAligned == true) {
                            WorldMatrices.clear();
                        }
                    }

                    if (TerrainQueryResource != nullptr) {
                        AppendFootCornerDebugLines(Ctx, *TerrainQueryResource, FootIKRuntimeComponent.mLeftFootEntityId, FootIKRuntimeComponent.mLeftToeEntityId, WorldMatrices);
                        AppendFootCornerDebugLines(Ctx, *TerrainQueryResource, FootIKRuntimeComponent.mRightFootEntityId, FootIKRuntimeComponent.mRightToeEntityId, WorldMatrices);
                    }
                }
            });
        }
    }
}
