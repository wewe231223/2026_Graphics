#include "PipelineFootIKSystem.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/FootIKRig.h"
#include "Game/Scene/Components/FootIKRuntime.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/IK/FootIKAlgorithms.h"
#include "Game/Scene/Pipeline/PipelineContext.h"
#include "Game/Terrain/TerrainQuery.h"
#include "Utility/MathValidation.h"

namespace Game {
    namespace Pipeline {
        namespace {
            struct FootIKSolveTarget final {
            public:
                bool mIsTargetResolved{};
                float mRawTargetOffset{};
                SimpleMath::Vector3 mGroundNormal{ SimpleMath::Vector3::Up };
                SimpleMath::Vector3 mTargetFootWorldPosition{};
            };

            SimpleMath::Matrix BuildLocalWorldMatrix(const Transform& TransformComponent) {
                const SimpleMath::Matrix TrsMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
                return TransformComponent.nodeToParent * TrsMatrix;
            }

            bool TryResolveWorldMatrix(PipelineContext& Ctx, Arche::EntityID EntityId, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, SimpleMath::Matrix& OutWorldMatrix) {
                const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CachedWorldMatrixIter{ InOutWorldMatrices.find(EntityId) };
                if (CachedWorldMatrixIter != InOutWorldMatrices.end()) {
                    OutWorldMatrix = CachedWorldMatrixIter->second;
                    return true;
                }

                std::vector<Arche::EntityID> EntityPath{};
                Arche::EntityID CurrentEntityId{ EntityId };
                SimpleMath::Matrix ParentWorldMatrix{ SimpleMath::Matrix::Identity };
                while (CurrentEntityId != Arche::NullEntityID && Ctx.ContainsEntity(CurrentEntityId) == true) {
                    const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CurrentCachedWorldMatrixIter{ InOutWorldMatrices.find(CurrentEntityId) };
                    if (CurrentCachedWorldMatrixIter != InOutWorldMatrices.end()) {
                        ParentWorldMatrix = CurrentCachedWorldMatrixIter->second;
                        break;
                    }

                    const Transform* TransformComponent{ Ctx.ReadComponent<Transform>(CurrentEntityId) };
                    const EntityHierarchy* HierarchyComponent{ Ctx.ReadComponent<EntityHierarchy>(CurrentEntityId) };
                    if (TransformComponent == nullptr || HierarchyComponent == nullptr) {
                        return false;
                    }

                    EntityPath.push_back(CurrentEntityId);
                    CurrentEntityId = HierarchyComponent->parent;
                }

                for (std::vector<Arche::EntityID>::const_reverse_iterator EntityPathIter{ EntityPath.crbegin() }; EntityPathIter != EntityPath.crend(); ++EntityPathIter) {
                    const Arche::EntityID CurrentPathEntityId{ *EntityPathIter };
                    const Transform* TransformComponent{ Ctx.ReadComponent<Transform>(CurrentPathEntityId) };
                    if (TransformComponent == nullptr) {
                        return false;
                    }

                    const SimpleMath::Matrix LocalWorldMatrix{ BuildLocalWorldMatrix(*TransformComponent) };
                    const SimpleMath::Matrix CurrentWorldMatrix{ LocalWorldMatrix * ParentWorldMatrix };
                    InOutWorldMatrices[CurrentPathEntityId] = CurrentWorldMatrix;
                    ParentWorldMatrix = CurrentWorldMatrix;
                }

                OutWorldMatrix = ParentWorldMatrix;
                return true;
            }

            Arche::EntityID FindBoneEntityByNameInHierarchy(PipelineContext& Ctx, Arche::EntityID RootEntityId, std::string_view TargetNameText) {
                if (RootEntityId == Arche::NullEntityID || TargetNameText.empty() == true || Ctx.ContainsEntity(RootEntityId) == false) {
                    return Arche::NullEntityID;
                }

                std::vector<Arche::EntityID> Stack{};
                Stack.reserve(256u);
                Stack.push_back(RootEntityId);
                while (Stack.empty() == false) {
                    const Arche::EntityID CurrentEntityId{ Stack.back() };
                    Stack.pop_back();
                    if (Ctx.ContainsEntity(CurrentEntityId) == false) {
                        continue;
                    }

                    const Bone* BoneComponent{ Ctx.ReadComponent<Bone>(CurrentEntityId) };
                    const Name* NameComponent{ Ctx.ReadComponent<Name>(CurrentEntityId) };
                    if (BoneComponent != nullptr && NameComponent != nullptr) {
                        const std::string_view CurrentNameText{ GetNameTextView(*NameComponent) };
                        if (CurrentNameText == TargetNameText) {
                            return CurrentEntityId;
                        }
                    }

                    const EntityHierarchy* HierarchyComponent{ Ctx.ReadComponent<EntityHierarchy>(CurrentEntityId) };
                    if (HierarchyComponent == nullptr) {
                        continue;
                    }

                    Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
                    while (ChildEntityId != Arche::NullEntityID) {
                        if (Ctx.ContainsEntity(ChildEntityId) == true) {
                            Stack.push_back(ChildEntityId);
                        }

                        const EntityHierarchy* ChildHierarchyComponent{ Ctx.ReadComponent<EntityHierarchy>(ChildEntityId) };
                        ChildEntityId = ChildHierarchyComponent == nullptr ? Arche::NullEntityID : ChildHierarchyComponent->nextSibling;
                    }
                }

                return Arche::NullEntityID;
            }

            bool IsCachedBoneEntityValid(PipelineContext& Ctx, Arche::EntityID EntityId, std::string_view ExpectedBoneNameText) {
                if (ExpectedBoneNameText.empty() == true) {
                    return EntityId == Arche::NullEntityID;
                }

                if (EntityId == Arche::NullEntityID || Ctx.ContainsEntity(EntityId) == false) {
                    return false;
                }

                const Bone* BoneComponent{ Ctx.ReadComponent<Bone>(EntityId) };
                const Name* NameComponent{ Ctx.ReadComponent<Name>(EntityId) };
                if (BoneComponent == nullptr || NameComponent == nullptr) {
                    return false;
                }

                return GetNameTextView(*NameComponent) == ExpectedBoneNameText;
            }

            void ResolveFootBoneEntities(PipelineContext& Ctx, const FootIKRig& FootIKRigComponent, Arche::EntityID BoneRootEntityId, FootIKRuntime& InOutRuntimeComponent) {
                InOutRuntimeComponent.mLeftFootEntityId = FindBoneEntityByNameInHierarchy(Ctx, BoneRootEntityId, GetFootIKRigBoneNameText(FootIKRigComponent.mLeftFootBoneName));
                InOutRuntimeComponent.mRightFootEntityId = FindBoneEntityByNameInHierarchy(Ctx, BoneRootEntityId, GetFootIKRigBoneNameText(FootIKRigComponent.mRightFootBoneName));
                InOutRuntimeComponent.mLeftToeEntityId = FindBoneEntityByNameInHierarchy(Ctx, BoneRootEntityId, GetFootIKRigBoneNameText(FootIKRigComponent.mLeftToeBoneName));
                InOutRuntimeComponent.mRightToeEntityId = FindBoneEntityByNameInHierarchy(Ctx, BoneRootEntityId, GetFootIKRigBoneNameText(FootIKRigComponent.mRightToeBoneName));
                InOutRuntimeComponent.mLeftShinEntityId = FindBoneEntityByNameInHierarchy(Ctx, BoneRootEntityId, GetFootIKRigBoneNameText(FootIKRigComponent.mLeftShinBoneName));
                InOutRuntimeComponent.mRightShinEntityId = FindBoneEntityByNameInHierarchy(Ctx, BoneRootEntityId, GetFootIKRigBoneNameText(FootIKRigComponent.mRightShinBoneName));
                InOutRuntimeComponent.mLeftThighEntityId = FindBoneEntityByNameInHierarchy(Ctx, BoneRootEntityId, GetFootIKRigBoneNameText(FootIKRigComponent.mLeftThighBoneName));
                InOutRuntimeComponent.mRightThighEntityId = FindBoneEntityByNameInHierarchy(Ctx, BoneRootEntityId, GetFootIKRigBoneNameText(FootIKRigComponent.mRightThighBoneName));
                InOutRuntimeComponent.mPelvisEntityId = FindBoneEntityByNameInHierarchy(Ctx, BoneRootEntityId, GetFootIKRigBoneNameText(FootIKRigComponent.mPelvisBoneName));
                InOutRuntimeComponent.mResolved = InOutRuntimeComponent.mLeftFootEntityId != Arche::NullEntityID || InOutRuntimeComponent.mRightFootEntityId != Arche::NullEntityID;
            }

            bool TryResolveTerrainTarget(PipelineContext& Ctx, const ITerrainQuery& TerrainQuery, Arche::EntityID FootEntityId, float MaxLift, float MaxDrop, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, FootIKSolveTarget& OutSolveTarget) {
                SimpleMath::Matrix FootWorldMatrix{};
                if (TryResolveWorldMatrix(Ctx, FootEntityId, InOutWorldMatrices, FootWorldMatrix) == false) {
                    return false;
                }

                SimpleMath::Vector3 FootWorldScale{};
                SimpleMath::Quaternion FootWorldRotation{};
                SimpleMath::Vector3 FootWorldPosition{};
                const bool IsDecomposed{ FootWorldMatrix.Decompose(FootWorldScale, FootWorldRotation, FootWorldPosition) };
                if (IsDecomposed == false || MathUtility::IsFiniteVector3(FootWorldPosition) == false) {
                    return false;
                }

                const float RayLength{ std::max(MaxLift, 0.0f) + std::max(MaxDrop, 0.0f) + 0.05f };
                if (RayLength <= 0.0f) {
                    return false;
                }

                const SimpleMath::Vector3 RayStartPoint{ FootWorldPosition + (SimpleMath::Vector3::Up * std::max(MaxLift, 0.0f)) };
                const SimpleMath::Ray Ray{ RayStartPoint, SimpleMath::Vector3::Down };
                SimpleMath::Vector3 HitPosition{};
                SimpleMath::Vector3 HitNormal{ SimpleMath::Vector3::Up };
                float HitDistance{};
                const bool IsHit{ TerrainQuery.TryRaycast(Ray, RayLength, HitPosition, HitNormal, HitDistance) };
                if (IsHit == false || MathUtility::IsFiniteVector3(HitPosition) == false || MathUtility::IsFiniteVector3(HitNormal) == false || MathUtility::IsFiniteFloat(HitDistance) == false) {
                    return false;
                }

                OutSolveTarget.mIsTargetResolved = true;
                OutSolveTarget.mRawTargetOffset = HitPosition.y - FootWorldPosition.y;
                OutSolveTarget.mGroundNormal = HitNormal;
                OutSolveTarget.mTargetFootWorldPosition = HitPosition;
                return true;
            }

            bool TryApplyOffsetToTransform(PipelineContext& Ctx, Arche::EntityID EntityId, float OffsetY) {
                Transform* TransformComponent{ Ctx.WriteComponent<Transform>(EntityId) };
                if (TransformComponent == nullptr || MathUtility::IsFiniteFloat(OffsetY) == false) {
                    return false;
                }

                TransformComponent->position.y += OffsetY;
                return true;
            }

            void AppendFootDebugRay(PipelineContext& Ctx, const ITerrainQuery& TerrainQuery, Arche::EntityID EntityId, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
                if (Ctx.HasRenderFlag(RFD::FrameGlobalFlagDrawDebugGeometry) == false) {
                    return;
                }

                SimpleMath::Matrix WorldMatrix{};
                if (TryResolveWorldMatrix(Ctx, EntityId, InOutWorldMatrices, WorldMatrix) == false) {
                    return;
                }

                SimpleMath::Vector3 WorldScale{};
                SimpleMath::Quaternion WorldRotation{};
                SimpleMath::Vector3 WorldPosition{};
                if (WorldMatrix.Decompose(WorldScale, WorldRotation, WorldPosition) == false || MathUtility::IsFiniteVector3(WorldPosition) == false) {
                    return;
                }

                constexpr float RayStartOffset{ 0.3f };
                constexpr float RayLength{ 0.5f };
                constexpr float LineThickness{ 0.0025f };
                constexpr SimpleMath::Vector4 RayColor{ 0.35f, 1.0f, 0.45f, 1.0f };
                constexpr SimpleMath::Vector4 HitColor{ 1.0f, 0.15f, 0.15f, 1.0f };
                constexpr SimpleMath::Vector4 HitNormalColor{ 0.15f, 0.65f, 1.0f, 1.0f };
                const SimpleMath::Vector3 RayStartPoint{ WorldPosition + (SimpleMath::Vector3::Up * RayStartOffset) };
                const SimpleMath::Ray Ray{ RayStartPoint, SimpleMath::Vector3::Down };
                SimpleMath::Vector3 HitPoint{};
                SimpleMath::Vector3 HitNormal{ SimpleMath::Vector3::Up };
                float HitDistance{};
                const bool IsHit{ TerrainQuery.TryRaycast(Ray, RayLength, HitPoint, HitNormal, HitDistance) };
                Ctx.GetRenderGatherResult().GetDebugGeometryContexts().push_back(RFD::DebugGeometryContext::CreateDirection(RayStartPoint, SimpleMath::Vector3::Down, RayLength, IsHit == true ? HitColor : RayColor, LineThickness));
                if (IsHit == true) {
                    Ctx.GetRenderGatherResult().GetDebugGeometryContexts().push_back(RFD::DebugGeometryContext::CreateDirection(HitPoint, HitNormal, 0.12f, HitNormalColor, LineThickness));
                }
            }
        }

        PipelineFootIKSystem::PipelineFootIKSystem() {
        }

        PipelineFootIKSystem::~PipelineFootIKSystem() {
        }

        PipelineFootIKSystem::PipelineFootIKSystem(const PipelineFootIKSystem& Other) {
            (void)Other;
        }

        PipelineFootIKSystem& PipelineFootIKSystem::operator=(const PipelineFootIKSystem& Other) {
            (void)Other;
            return *this;
        }

        PipelineFootIKSystem::PipelineFootIKSystem(PipelineFootIKSystem&& Other) noexcept {
            (void)Other;
        }

        PipelineFootIKSystem& PipelineFootIKSystem::operator=(PipelineFootIKSystem&& Other) noexcept {
            (void)Other;
            return *this;
        }

        const std::string& PipelineFootIKSystem::Name() const {
            static const std::string NameText{ "FootIKSystem" };
            return NameText;
        }

        void PipelineFootIKSystem::Execute(PipelineContext& Ctx, float Dt) {
            const ITerrainQuery* TerrainQueryResource{ Ctx.GetTerrainQuery() };
            if (TerrainQueryResource == nullptr) {
                return;
            }

            std::unordered_map<Arche::EntityID, SimpleMath::Matrix> WorldMatrices{};
            WorldMatrices.reserve(1024u);
            Ctx.ForEach<Animator, FootIKRig, BoneSkinReference, Transform, FootIKRuntime, EntityHierarchy>([&](Animator& AnimatorComponent, FootIKRig& FootIKRigComponent, BoneSkinReference& BoneSkinReferenceComponent, Transform& TransformComponent, FootIKRuntime& RuntimeComponent, EntityHierarchy& HierarchyComponent) {
                (void)AnimatorComponent;
                (void)TransformComponent;
                (void)HierarchyComponent;

                const std::string_view LeftFootBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mLeftFootBoneName) };
                const std::string_view RightFootBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mRightFootBoneName) };
                const std::string_view LeftToeBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mLeftToeBoneName) };
                const std::string_view RightToeBoneNameText{ GetFootIKRigBoneNameText(FootIKRigComponent.mRightToeBoneName) };
                const bool IsRuntimeValid{ IsCachedBoneEntityValid(Ctx, RuntimeComponent.mLeftFootEntityId, LeftFootBoneNameText) == true && IsCachedBoneEntityValid(Ctx, RuntimeComponent.mRightFootEntityId, RightFootBoneNameText) == true && IsCachedBoneEntityValid(Ctx, RuntimeComponent.mLeftToeEntityId, LeftToeBoneNameText) == true && IsCachedBoneEntityValid(Ctx, RuntimeComponent.mRightToeEntityId, RightToeBoneNameText) == true };
                if (IsRuntimeValid == false) {
                    RuntimeComponent.mResolved = false;
                }

                if (RuntimeComponent.mResolved == false) {
                    ResolveFootBoneEntities(Ctx, FootIKRigComponent, BoneSkinReferenceComponent.boneRootEntityId, RuntimeComponent);
                }

                FootIKSolveTarget LeftSolveTarget{};
                FootIKSolveTarget RightSolveTarget{};
                if (FootIKRigComponent.mEnabled == true && RuntimeComponent.mResolved == true) {
                    static_cast<void>(TryResolveTerrainTarget(Ctx, *TerrainQueryResource, RuntimeComponent.mLeftFootEntityId, FootIKRigComponent.mMaxLift, FootIKRigComponent.mMaxDrop, WorldMatrices, LeftSolveTarget));
                    static_cast<void>(TryResolveTerrainTarget(Ctx, *TerrainQueryResource, RuntimeComponent.mRightFootEntityId, FootIKRigComponent.mMaxLift, FootIKRigComponent.mMaxDrop, WorldMatrices, RightSolveTarget));
                }

                const float MaxLift{ std::max(FootIKRigComponent.mMaxLift, 0.0f) };
                const float MaxDrop{ std::max(FootIKRigComponent.mMaxDrop, 0.0f) };
                const float LeftTargetOffset{ LeftSolveTarget.mIsTargetResolved == true ? std::clamp(LeftSolveTarget.mRawTargetOffset, -MaxDrop, MaxLift) : 0.0f };
                const float RightTargetOffset{ RightSolveTarget.mIsTargetResolved == true ? std::clamp(RightSolveTarget.mRawTargetOffset, -MaxDrop, MaxLift) : 0.0f };
                const float PreviousLeftOffset{ MathUtility::IsFiniteFloat(RuntimeComponent.mLeftCurrentOffset) == true ? RuntimeComponent.mLeftCurrentOffset : 0.0f };
                const float PreviousRightOffset{ MathUtility::IsFiniteFloat(RuntimeComponent.mRightCurrentOffset) == true ? RuntimeComponent.mRightCurrentOffset : 0.0f };
                const float SmoothedLeftOffset{ IK::ResolveSmoothedOffset(PreviousLeftOffset, LeftTargetOffset, FootIKRigComponent.mBlendSpeed, Dt) };
                const float SmoothedRightOffset{ IK::ResolveSmoothedOffset(PreviousRightOffset, RightTargetOffset, FootIKRigComponent.mBlendSpeed, Dt) };
                const float LeftOffsetDelta{ SmoothedLeftOffset - PreviousLeftOffset };
                const float RightOffsetDelta{ SmoothedRightOffset - PreviousRightOffset };
                RuntimeComponent.mLeftCurrentOffset = MathUtility::IsFiniteFloat(SmoothedLeftOffset) == true ? SmoothedLeftOffset : 0.0f;
                RuntimeComponent.mRightCurrentOffset = MathUtility::IsFiniteFloat(SmoothedRightOffset) == true ? SmoothedRightOffset : 0.0f;

                if (FootIKRigComponent.mEnabled == true && RuntimeComponent.mResolved == true) {
                    if (LeftSolveTarget.mIsTargetResolved == true && TryApplyOffsetToTransform(Ctx, RuntimeComponent.mLeftFootEntityId, LeftOffsetDelta) == true) {
                        WorldMatrices.clear();
                    }

                    if (RightSolveTarget.mIsTargetResolved == true && TryApplyOffsetToTransform(Ctx, RuntimeComponent.mRightFootEntityId, RightOffsetDelta) == true) {
                        WorldMatrices.clear();
                    }

                    AppendFootDebugRay(Ctx, *TerrainQueryResource, RuntimeComponent.mLeftFootEntityId, WorldMatrices);
                    AppendFootDebugRay(Ctx, *TerrainQueryResource, RuntimeComponent.mRightFootEntityId, WorldMatrices);
                    AppendFootDebugRay(Ctx, *TerrainQueryResource, RuntimeComponent.mLeftToeEntityId, WorldMatrices);
                    AppendFootDebugRay(Ctx, *TerrainQueryResource, RuntimeComponent.mRightToeEntityId, WorldMatrices);
                }
            });
        }
    }
}
