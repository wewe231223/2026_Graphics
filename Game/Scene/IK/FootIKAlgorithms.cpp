#include "Game/Scene/IK/FootIKAlgorithms.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/TerrainCollidee.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/IK/FootIKSolver.h"
#include "Game/Scene/TerrainHeightResolver.h"

namespace {
    constexpr float FootOffsetEpsilon{ 1.0e-4f };
    constexpr float SurfaceNormalLengthEpsilon{ 1.0e-6f };
    constexpr float FootPlantWeightEpsilon{ 1.0e-3f };
    constexpr float FootPlantReleaseOffset{ -0.08f };
    constexpr float FootPlantEngageOffset{ 0.01f };
    constexpr float MaxFootTiltRadians{ 0.6108652382f };
    constexpr float PelvisWeight{ 0.50f };
    constexpr int FootIKFabrikMaxIterationCount{ 12 };
    constexpr float FootIKFabrikConvergenceDistance{ 1.0e-3f };
    constexpr ::std::size_t KneeJointIndex{ 1 };
    constexpr float KneeMinimumAngleRadians{ 0.0872664626f };
    constexpr float KneeMaximumAngleRadians{ 3.0543261909f };

    bool IsFiniteFloat(const float Value) {
        return ::std::isfinite(Value) != 0;
    }

    bool IsFiniteVector3(const SimpleMath::Vector3& Value) {
        return IsFiniteFloat(Value.x) && IsFiniteFloat(Value.y) && IsFiniteFloat(Value.z);
    }

    bool IsFiniteQuaternion(const SimpleMath::Quaternion& Value) {
        return IsFiniteFloat(Value.x) && IsFiniteFloat(Value.y) && IsFiniteFloat(Value.z) && IsFiniteFloat(Value.w);
    }

    SimpleMath::Vector3 ResolveCrossProduct(const SimpleMath::Vector3& Left, const SimpleMath::Vector3& Right) {
        return SimpleMath::Vector3{ (Left.y * Right.z) - (Left.z * Right.y), (Left.z * Right.x) - (Left.x * Right.z), (Left.x * Right.y) - (Left.y * Right.x) };
    }

    bool TryResolveNormalizedVector(const SimpleMath::Vector3& SourceVector, SimpleMath::Vector3& OutNormalizedVector) {
        if (IsFiniteVector3(SourceVector) == false) {
            return false;
        }

        const float SourceVectorLengthSquared{ SourceVector.LengthSquared() };
        if (IsFiniteFloat(SourceVectorLengthSquared) == false || SourceVectorLengthSquared <= SurfaceNormalLengthEpsilon) {
            return false;
        }

        OutNormalizedVector = SourceVector;
        OutNormalizedVector.Normalize();
        return IsFiniteVector3(OutNormalizedVector);
    }

    bool TryResolveNormalizedQuaternion(const SimpleMath::Quaternion& SourceQuaternion, SimpleMath::Quaternion& OutNormalizedQuaternion) {
        if (IsFiniteQuaternion(SourceQuaternion) == false) {
            return false;
        }

        const float SourceQuaternionLengthSquared{ (SourceQuaternion.x * SourceQuaternion.x) + (SourceQuaternion.y * SourceQuaternion.y) + (SourceQuaternion.z * SourceQuaternion.z) + (SourceQuaternion.w * SourceQuaternion.w) };
        if (IsFiniteFloat(SourceQuaternionLengthSquared) == false || SourceQuaternionLengthSquared <= SurfaceNormalLengthEpsilon) {
            return false;
        }

        OutNormalizedQuaternion = SourceQuaternion;
        OutNormalizedQuaternion.Normalize();
        return IsFiniteQuaternion(OutNormalizedQuaternion);
    }

    bool TryResolveFromToRotation(const SimpleMath::Vector3& SourceDirection, const SimpleMath::Vector3& TargetDirection, SimpleMath::Quaternion& OutRotation) {
        SimpleMath::Vector3 SafeSourceDirection{};
        SimpleMath::Vector3 SafeTargetDirection{};
        if (TryResolveNormalizedVector(SourceDirection, SafeSourceDirection) == false || TryResolveNormalizedVector(TargetDirection, SafeTargetDirection) == false) {
            return false;
        }

        const float DirectionDot{ ::std::clamp(SafeSourceDirection.Dot(SafeTargetDirection), -1.0f, 1.0f) };
        if (DirectionDot >= (1.0f - 1.0e-5f)) {
            OutRotation = SimpleMath::Quaternion::Identity;
            return true;
        }

        if (DirectionDot <= (-1.0f + 1.0e-5f)) {
            SimpleMath::Vector3 RotationAxis{ ResolveCrossProduct(SafeSourceDirection, SimpleMath::Vector3::Right) };
            if (TryResolveNormalizedVector(RotationAxis, RotationAxis) == false) {
                RotationAxis = ResolveCrossProduct(SafeSourceDirection, SimpleMath::Vector3::Up);
            }

            if (TryResolveNormalizedVector(RotationAxis, RotationAxis) == false) {
                RotationAxis = ResolveCrossProduct(SafeSourceDirection, SimpleMath::Vector3::Forward);
            }

            if (TryResolveNormalizedVector(RotationAxis, RotationAxis) == false) {
                return false;
            }

            const SimpleMath::Quaternion RotationForOppositeDirection{ SimpleMath::Quaternion::CreateFromAxisAngle(RotationAxis, DirectX::XM_PI) };
            return TryResolveNormalizedQuaternion(RotationForOppositeDirection, OutRotation);
        }

        const SimpleMath::Vector3 RotationAxis{ ResolveCrossProduct(SafeSourceDirection, SafeTargetDirection) };
        SimpleMath::Vector3 SafeRotationAxis{};
        if (TryResolveNormalizedVector(RotationAxis, SafeRotationAxis) == false) {
            OutRotation = SimpleMath::Quaternion::Identity;
            return true;
        }

        const float RotationAngleRadians{ ::std::acos(DirectionDot) };
        const SimpleMath::Quaternion AxisAngleRotation{ SimpleMath::Quaternion::CreateFromAxisAngle(SafeRotationAxis, RotationAngleRadians) };
        return TryResolveNormalizedQuaternion(AxisAngleRotation, OutRotation);
    }

    bool TryResolveClampedFromToRotation(const SimpleMath::Vector3& SourceDirection, const SimpleMath::Vector3& TargetDirection, const float MaxRotationRadians, SimpleMath::Quaternion& OutRotation) {
        SimpleMath::Vector3 SafeSourceDirection{};
        SimpleMath::Vector3 SafeTargetDirection{};
        if (TryResolveNormalizedVector(SourceDirection, SafeSourceDirection) == false || TryResolveNormalizedVector(TargetDirection, SafeTargetDirection) == false || IsFiniteFloat(MaxRotationRadians) == false) {
            return false;
        }

        const float SafeMaxRotationRadians{ (::std::max)(MaxRotationRadians, 0.0f) };
        const float DirectionDot{ ::std::clamp(SafeSourceDirection.Dot(SafeTargetDirection), -1.0f, 1.0f) };
        const float RequiredRotationRadians{ ::std::acos(DirectionDot) };
        if (SafeMaxRotationRadians <= 0.0f || RequiredRotationRadians <= SafeMaxRotationRadians) {
            return TryResolveFromToRotation(SafeSourceDirection, SafeTargetDirection, OutRotation);
        }

        SimpleMath::Vector3 RotationAxis{ ResolveCrossProduct(SafeSourceDirection, SafeTargetDirection) };
        if (TryResolveNormalizedVector(RotationAxis, RotationAxis) == false) {
            RotationAxis = ResolveCrossProduct(SafeSourceDirection, SimpleMath::Vector3::Right);
        }

        if (TryResolveNormalizedVector(RotationAxis, RotationAxis) == false) {
            RotationAxis = ResolveCrossProduct(SafeSourceDirection, SimpleMath::Vector3::Up);
        }

        if (TryResolveNormalizedVector(RotationAxis, RotationAxis) == false) {
            RotationAxis = ResolveCrossProduct(SafeSourceDirection, SimpleMath::Vector3::Forward);
        }

        if (TryResolveNormalizedVector(RotationAxis, RotationAxis) == false) {
            return false;
        }

        const SimpleMath::Quaternion AxisAngleRotation{ SimpleMath::Quaternion::CreateFromAxisAngle(RotationAxis, SafeMaxRotationRadians) };
        return TryResolveNormalizedQuaternion(AxisAngleRotation, OutRotation);
    }

    bool TryResolveWorldRotationWithWorldDelta(const SimpleMath::Quaternion& CurrentWorldRotation, const SimpleMath::Quaternion& WorldDeltaRotation, SimpleMath::Quaternion& OutWorldRotation) {
        SimpleMath::Quaternion SafeCurrentWorldRotation{};
        SimpleMath::Quaternion SafeWorldDeltaRotation{};
        if (TryResolveNormalizedQuaternion(CurrentWorldRotation, SafeCurrentWorldRotation) == false || TryResolveNormalizedQuaternion(WorldDeltaRotation, SafeWorldDeltaRotation) == false) {
            return false;
        }

        const SimpleMath::Quaternion WorldDeltaAppliedRotation{ SafeCurrentWorldRotation * SafeWorldDeltaRotation };
        return TryResolveNormalizedQuaternion(WorldDeltaAppliedRotation, OutWorldRotation);
    }

    float ResolveWorldObbBottomY(const DirectX::BoundingOrientedBox& WorldObb) {
        DirectX::XMFLOAT3 Corners[8]{};
        WorldObb.GetCorners(Corners);

        float MinimumY{ Corners[0].y };
        for (::std::size_t CornerIndex{ 1 }; CornerIndex < ::std::size(Corners); ++CornerIndex) {
            if (Corners[CornerIndex].y < MinimumY) {
                MinimumY = Corners[CornerIndex].y;
            }
        }

        return MinimumY;
    }

    SimpleMath::Matrix BuildLocalWorldMatrix(const Game::Transform& TransformComponent) {
        const SimpleMath::Matrix TrsMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
        return TransformComponent.nodeToParent * TrsMatrix;
    }

    bool TryResolveWorldMatrix(Arche::World& World, const Arche::EntityID EntityId, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, SimpleMath::Matrix& OutWorldMatrix) {
        const ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CachedWorldMatrixIter{ InOutWorldMatrices.find(EntityId) };
        if (CachedWorldMatrixIter != InOutWorldMatrices.end()) {
            OutWorldMatrix = CachedWorldMatrixIter->second;
            return true;
        }

        ::std::vector<Arche::EntityID> EntityPath{};
        Arche::EntityID CurrentEntityId{ EntityId };
        SimpleMath::Matrix ParentWorldMatrix{ SimpleMath::Matrix::Identity };
        while (CurrentEntityId != Arche::NullEntityID) {
            const ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CurrentCachedWorldMatrixIter{ InOutWorldMatrices.find(CurrentEntityId) };
            if (CurrentCachedWorldMatrixIter != InOutWorldMatrices.end()) {
                ParentWorldMatrix = CurrentCachedWorldMatrixIter->second;
                break;
            }

            const Game::Transform* TransformComponent{ ::std::as_const(World).GetComponent<Game::Transform>(CurrentEntityId) };
            const Game::EntityHierarchy* HierarchyComponent{ ::std::as_const(World).GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (TransformComponent == nullptr || HierarchyComponent == nullptr) {
                return false;
            }

            EntityPath.push_back(CurrentEntityId);
            CurrentEntityId = HierarchyComponent->parent;
        }

        for (::std::vector<Arche::EntityID>::const_reverse_iterator EntityPathIter{ EntityPath.crbegin() }; EntityPathIter != EntityPath.crend(); ++EntityPathIter) {
            const Arche::EntityID CurrentPathEntityId{ *EntityPathIter };
            const Game::Transform* TransformComponent{ ::std::as_const(World).GetComponent<Game::Transform>(CurrentPathEntityId) };
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

    Arche::EntityID FindBoneEntityByNameInHierarchy(const Arche::World::WorldReadOnlyView& ReadOnlyWorld, const Arche::EntityID RootEntityId, const ::std::string_view TargetNameText) {
        if (RootEntityId == Arche::NullEntityID || TargetNameText.empty() == true) {
            return Arche::NullEntityID;
        }

        ::std::vector<Arche::EntityID> Stack{};
        Stack.reserve(256);
        Stack.push_back(RootEntityId);
        while (Stack.empty() == false) {
            const Arche::EntityID CurrentEntityId{ Stack.back() };
            Stack.pop_back();

            const Game::Bone* BoneComponent{ ReadOnlyWorld.GetComponent<Game::Bone>(CurrentEntityId) };
            const Game::Name* NameComponent{ ReadOnlyWorld.GetComponent<Game::Name>(CurrentEntityId) };
            if (BoneComponent != nullptr && NameComponent != nullptr) {
                const ::std::string_view CurrentNameText{ Game::GetNameTextView(*NameComponent) };
                if (CurrentNameText == TargetNameText) {
                    return CurrentEntityId;
                }
            }

            const Game::EntityHierarchy* HierarchyComponent{ ReadOnlyWorld.GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (HierarchyComponent == nullptr) {
                continue;
            }

            Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
            while (ChildEntityId != Arche::NullEntityID) {
                Stack.push_back(ChildEntityId);

                const Game::EntityHierarchy* ChildHierarchyComponent{ ReadOnlyWorld.GetComponent<Game::EntityHierarchy>(ChildEntityId) };
                ChildEntityId = ChildHierarchyComponent == nullptr ? Arche::NullEntityID : ChildHierarchyComponent->nextSibling;
            }
        }

        return Arche::NullEntityID;
    }

    bool TryResolveTerrainGround(Arche::World& World, const SimpleMath::Vector3& Position, float& OutGroundY, SimpleMath::Vector3& OutGroundNormal) {
        bool IsResolved{};
        float HighestGroundY{};
        SimpleMath::Vector3 HighestGroundNormal{ SimpleMath::Vector3::Up };
        for (const auto [TerrainCollideeComponent] : World.Query<Game::TerrainCollidee>()) {
            Game::TerrainHeightResolver* TerrainHeightResolverPointer{ TerrainCollideeComponent.mTerrainHeightResolver };
            if (TerrainHeightResolverPointer == nullptr) {
                continue;
            }

            SimpleMath::Vector3 CandidatePosition{ Position };
            SimpleMath::Vector3 CandidateGroundNormal{ SimpleMath::Vector3::Up };
            if (TerrainHeightResolverPointer->TryResolvePositionYAndNormal(CandidatePosition, CandidateGroundNormal) == false || IsFiniteVector3(CandidatePosition) == false || IsFiniteVector3(CandidateGroundNormal) == false) {
                continue;
            }

            const float CandidateGroundNormalLengthSquared{ CandidateGroundNormal.LengthSquared() };
            if (IsFiniteFloat(CandidateGroundNormalLengthSquared) == false || CandidateGroundNormalLengthSquared <= SurfaceNormalLengthEpsilon) {
                continue;
            }

            CandidateGroundNormal.Normalize();
            if (IsResolved == false || CandidatePosition.y > HighestGroundY) {
                HighestGroundY = CandidatePosition.y;
                HighestGroundNormal = CandidateGroundNormal;
                IsResolved = true;
            }
        }

        if (IsResolved == false || IsFiniteFloat(HighestGroundY) == false || IsFiniteVector3(HighestGroundNormal) == false) {
            return false;
        }

        OutGroundY = HighestGroundY;
        OutGroundNormal = HighestGroundNormal;
        return true;
    }

    bool IsCachedBoneEntityValid(const Arche::World::WorldReadOnlyView& ReadOnlyWorld, const Arche::EntityID EntityId, const ::std::string_view ExpectedBoneNameText) {
        if (ExpectedBoneNameText.empty() == true) {
            return EntityId == Arche::NullEntityID;
        }

        if (EntityId == Arche::NullEntityID) {
            return false;
        }

        const Game::Bone* BoneComponent{ ReadOnlyWorld.GetComponent<Game::Bone>(EntityId) };
        const Game::Name* NameComponent{ ReadOnlyWorld.GetComponent<Game::Name>(EntityId) };
        if (BoneComponent == nullptr || NameComponent == nullptr) {
            return false;
        }

        const ::std::string_view CurrentNameText{ Game::GetNameTextView(*NameComponent) };
        return CurrentNameText == ExpectedBoneNameText;
    }

    void ResolveFootBoneEntities(const Arche::World::WorldReadOnlyView& ReadOnlyWorld, const Game::FootIKRig& FootIKRigComponent, const Arche::EntityID BoneRootEntityId, Game::FootIKRuntime& InOutFootIKRuntimeComponent) {
        InOutFootIKRuntimeComponent.mLeftFootEntityId = Arche::NullEntityID;
        InOutFootIKRuntimeComponent.mRightFootEntityId = Arche::NullEntityID;
        InOutFootIKRuntimeComponent.mLeftToeEntityId = Arche::NullEntityID;
        InOutFootIKRuntimeComponent.mRightToeEntityId = Arche::NullEntityID;
        InOutFootIKRuntimeComponent.mLeftShinEntityId = Arche::NullEntityID;
        InOutFootIKRuntimeComponent.mRightShinEntityId = Arche::NullEntityID;
        InOutFootIKRuntimeComponent.mLeftThighEntityId = Arche::NullEntityID;
        InOutFootIKRuntimeComponent.mRightThighEntityId = Arche::NullEntityID;
        InOutFootIKRuntimeComponent.mPelvisEntityId = Arche::NullEntityID;
        InOutFootIKRuntimeComponent.mResolved = false;
        if (BoneRootEntityId == Arche::NullEntityID) {
            return;
        }

        const ::std::string_view LeftFootBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mLeftFootBoneName) };
        const ::std::string_view RightFootBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mRightFootBoneName) };
        const ::std::string_view LeftToeBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mLeftToeBoneName) };
        const ::std::string_view RightToeBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mRightToeBoneName) };
        const ::std::string_view LeftShinBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mLeftShinBoneName) };
        const ::std::string_view RightShinBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mRightShinBoneName) };
        const ::std::string_view LeftThighBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mLeftThighBoneName) };
        const ::std::string_view RightThighBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mRightThighBoneName) };
        const ::std::string_view PelvisBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mPelvisBoneName) };

        InOutFootIKRuntimeComponent.mLeftFootEntityId = FindBoneEntityByNameInHierarchy(ReadOnlyWorld, BoneRootEntityId, LeftFootBoneNameText);
        InOutFootIKRuntimeComponent.mRightFootEntityId = FindBoneEntityByNameInHierarchy(ReadOnlyWorld, BoneRootEntityId, RightFootBoneNameText);
        InOutFootIKRuntimeComponent.mLeftToeEntityId = FindBoneEntityByNameInHierarchy(ReadOnlyWorld, BoneRootEntityId, LeftToeBoneNameText);
        InOutFootIKRuntimeComponent.mRightToeEntityId = FindBoneEntityByNameInHierarchy(ReadOnlyWorld, BoneRootEntityId, RightToeBoneNameText);
        InOutFootIKRuntimeComponent.mLeftShinEntityId = FindBoneEntityByNameInHierarchy(ReadOnlyWorld, BoneRootEntityId, LeftShinBoneNameText);
        InOutFootIKRuntimeComponent.mRightShinEntityId = FindBoneEntityByNameInHierarchy(ReadOnlyWorld, BoneRootEntityId, RightShinBoneNameText);
        InOutFootIKRuntimeComponent.mLeftThighEntityId = FindBoneEntityByNameInHierarchy(ReadOnlyWorld, BoneRootEntityId, LeftThighBoneNameText);
        InOutFootIKRuntimeComponent.mRightThighEntityId = FindBoneEntityByNameInHierarchy(ReadOnlyWorld, BoneRootEntityId, RightThighBoneNameText);
        InOutFootIKRuntimeComponent.mPelvisEntityId = FindBoneEntityByNameInHierarchy(ReadOnlyWorld, BoneRootEntityId, PelvisBoneNameText);
        InOutFootIKRuntimeComponent.mResolved = true;
    }

    bool TryResolveFootSoleY(Arche::World& World, const Arche::EntityID FootEntityId, const SimpleMath::Matrix& FootWorldMatrix, const SimpleMath::Vector3& FootWorldPosition, float& OutFootSoleY) {
        const Game::BoundingBox* BoundingBoxComponent{ ::std::as_const(World).GetComponent<Game::BoundingBox>(FootEntityId) };
        if (BoundingBoxComponent == nullptr) {
            OutFootSoleY = FootWorldPosition.y;
            return IsFiniteFloat(OutFootSoleY);
        }

        DirectX::BoundingOrientedBox FootWorldObb{};
        BoundingBoxComponent->GetObb().Transform(FootWorldObb, FootWorldMatrix);
        const float FootSoleY{ ResolveWorldObbBottomY(FootWorldObb) };
        if (IsFiniteFloat(FootSoleY) == false) {
            return false;
        }

        OutFootSoleY = FootSoleY;
        return true;
    }

    bool TryResolveFootTargetOffset(Arche::World& World, const Arche::EntityID FootEntityId, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, float& OutTargetOffsetY, SimpleMath::Vector3& OutGroundNormal) {
        if (FootEntityId == Arche::NullEntityID) {
            return false;
        }

        SimpleMath::Matrix FootWorldMatrix{};
        if (TryResolveWorldMatrix(World, FootEntityId, InOutWorldMatrices, FootWorldMatrix) == false) {
            return false;
        }

        const SimpleMath::Vector3 FootWorldPosition{ FootWorldMatrix._41, FootWorldMatrix._42, FootWorldMatrix._43 };
        if (IsFiniteVector3(FootWorldPosition) == false) {
            return false;
        }

        float GroundY{};
        SimpleMath::Vector3 GroundNormal{ SimpleMath::Vector3::Up };
        if (TryResolveTerrainGround(World, FootWorldPosition, GroundY, GroundNormal) == false) {
            return false;
        }

        float FootSoleY{};
        if (TryResolveFootSoleY(World, FootEntityId, FootWorldMatrix, FootWorldPosition, FootSoleY) == false) {
            return false;
        }

        const float TargetOffsetY{ GroundY - FootSoleY };
        if (IsFiniteFloat(TargetOffsetY) == false) {
            return false;
        }

        OutTargetOffsetY = TargetOffsetY;
        OutGroundNormal = GroundNormal;
        return true;
    }

    bool TryApplyWorldTransformToBoneTransform(Arche::World& World, const Arche::EntityID BoneEntityId, const SimpleMath::Vector3& DesiredWorldPosition, const SimpleMath::Quaternion& DesiredWorldRotation, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        if (BoneEntityId == Arche::NullEntityID || IsFiniteVector3(DesiredWorldPosition) == false || IsFiniteQuaternion(DesiredWorldRotation) == false) {
            return false;
        }

        SimpleMath::Quaternion SafeDesiredWorldRotation{};
        if (TryResolveNormalizedQuaternion(DesiredWorldRotation, SafeDesiredWorldRotation) == false) {
            return false;
        }

        Game::Transform* BoneTransformComponent{ World.GetComponent<Game::Transform>(BoneEntityId) };
        const Game::EntityHierarchy* BoneHierarchyComponent{ ::std::as_const(World).GetComponent<Game::EntityHierarchy>(BoneEntityId) };
        if (BoneTransformComponent == nullptr || BoneHierarchyComponent == nullptr) {
            return false;
        }

        SimpleMath::Matrix BoneWorldMatrix{};
        if (TryResolveWorldMatrix(World, BoneEntityId, InOutWorldMatrices, BoneWorldMatrix) == false) {
            return false;
        }

        SimpleMath::Vector3 CurrentWorldScale{};
        SimpleMath::Quaternion CurrentWorldRotation{};
        SimpleMath::Vector3 CurrentWorldPosition{};
        const bool IsCurrentWorldDecomposeSucceeded{ BoneWorldMatrix.Decompose(CurrentWorldScale, CurrentWorldRotation, CurrentWorldPosition) };
        if (IsCurrentWorldDecomposeSucceeded == false || IsFiniteVector3(CurrentWorldScale) == false) {
            return false;
        }

        SimpleMath::Matrix ParentWorldMatrix{ SimpleMath::Matrix::Identity };
        if (BoneHierarchyComponent->parent != Arche::NullEntityID) {
            if (TryResolveWorldMatrix(World, BoneHierarchyComponent->parent, InOutWorldMatrices, ParentWorldMatrix) == false) {
                return false;
            }
        }

        const SimpleMath::Matrix DesiredBoneWorldMatrix{ SimpleMath::Matrix::CreateScale(CurrentWorldScale) * SimpleMath::Matrix::CreateFromQuaternion(SafeDesiredWorldRotation) * SimpleMath::Matrix::CreateTranslation(DesiredWorldPosition) };
        SimpleMath::Matrix ParentWorldInverseMatrix{ ParentWorldMatrix };
        ParentWorldInverseMatrix = ParentWorldInverseMatrix.Invert();
        if (IsFiniteFloat(ParentWorldInverseMatrix._11) == false) {
            return false;
        }

        const SimpleMath::Matrix DesiredBoneLocalWorldMatrix{ DesiredBoneWorldMatrix * ParentWorldInverseMatrix };
        SimpleMath::Matrix NodeToParentInverseMatrix{ BoneTransformComponent->nodeToParent };
        NodeToParentInverseMatrix = NodeToParentInverseMatrix.Invert();
        if (IsFiniteFloat(NodeToParentInverseMatrix._11) == false) {
            return false;
        }

        SimpleMath::Matrix DesiredTrsMatrix{ NodeToParentInverseMatrix * DesiredBoneLocalWorldMatrix };
        SimpleMath::Vector3 DecomposedScale{};
        SimpleMath::Quaternion DecomposedRotation{};
        SimpleMath::Vector3 DecomposedPosition{};
        const bool IsDesiredTrsDecomposeSucceeded{ DesiredTrsMatrix.Decompose(DecomposedScale, DecomposedRotation, DecomposedPosition) };
        if (IsDesiredTrsDecomposeSucceeded == false || IsFiniteVector3(DecomposedPosition) == false || IsFiniteQuaternion(DecomposedRotation) == false) {
            return false;
        }

        DecomposedRotation.Normalize();
        BoneTransformComponent->position = DecomposedPosition;
        BoneTransformComponent->rotation = DecomposedRotation;
        BoneTransformComponent->UpdateEulerRadiansFromRotation();
        InOutWorldMatrices[BoneEntityId] = DesiredBoneWorldMatrix;
        return true;
    }

    bool TryApplyOffsetToBoneTransform(Arche::World& World, const Arche::EntityID BoneEntityId, const float OffsetY, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        if (BoneEntityId == Arche::NullEntityID || IsFiniteFloat(OffsetY) == false) {
            return false;
        }

        const float SafeOffsetY{ ::std::abs(OffsetY) <= FootOffsetEpsilon ? 0.0f : OffsetY };
        if (SafeOffsetY == 0.0f) {
            return false;
        }

        SimpleMath::Matrix BoneWorldMatrix{};
        if (TryResolveWorldMatrix(World, BoneEntityId, InOutWorldMatrices, BoneWorldMatrix) == false) {
            return false;
        }

        SimpleMath::Vector3 CurrentWorldScale{};
        SimpleMath::Quaternion CurrentWorldRotation{};
        SimpleMath::Vector3 CurrentWorldPosition{};
        const bool IsCurrentWorldDecomposeSucceeded{ BoneWorldMatrix.Decompose(CurrentWorldScale, CurrentWorldRotation, CurrentWorldPosition) };
        if (IsCurrentWorldDecomposeSucceeded == false || IsFiniteVector3(CurrentWorldScale) == false || IsFiniteVector3(CurrentWorldPosition) == false || IsFiniteQuaternion(CurrentWorldRotation) == false) {
            return false;
        }

        if (TryResolveNormalizedQuaternion(CurrentWorldRotation, CurrentWorldRotation) == false) {
            return false;
        }

        SimpleMath::Vector3 DesiredWorldPosition{ CurrentWorldPosition };
        DesiredWorldPosition.y += SafeOffsetY;
        return TryApplyWorldTransformToBoneTransform(World, BoneEntityId, DesiredWorldPosition, CurrentWorldRotation, InOutWorldMatrices);
    }

    bool TryApplyWorldRotationToBoneTransform(Arche::World& World, const Arche::EntityID BoneEntityId, const SimpleMath::Quaternion& DesiredWorldRotation, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        if (BoneEntityId == Arche::NullEntityID || IsFiniteQuaternion(DesiredWorldRotation) == false) {
            return false;
        }

        SimpleMath::Matrix BoneWorldMatrix{};
        if (TryResolveWorldMatrix(World, BoneEntityId, InOutWorldMatrices, BoneWorldMatrix) == false) {
            return false;
        }

        SimpleMath::Vector3 CurrentWorldScale{};
        SimpleMath::Quaternion CurrentWorldRotation{};
        SimpleMath::Vector3 CurrentWorldPosition{};
        const bool IsCurrentWorldDecomposeSucceeded{ BoneWorldMatrix.Decompose(CurrentWorldScale, CurrentWorldRotation, CurrentWorldPosition) };
        if (IsCurrentWorldDecomposeSucceeded == false || IsFiniteVector3(CurrentWorldScale) == false || IsFiniteVector3(CurrentWorldPosition) == false) {
            return false;
        }

        return TryApplyWorldTransformToBoneTransform(World, BoneEntityId, CurrentWorldPosition, DesiredWorldRotation, InOutWorldMatrices);
    }

    bool TryResolveKneePreferredBendDirection(const ::std::array<SimpleMath::Vector3, 3>& CurrentWorldPositions, SimpleMath::Vector3& OutPreferredBendDirection) {
        const SimpleMath::Vector3 RootToEndVector{ CurrentWorldPositions[2] - CurrentWorldPositions[0] };
        SimpleMath::Vector3 RootToEndDirection{};
        if (TryResolveNormalizedVector(RootToEndVector, RootToEndDirection) == false) {
            return false;
        }

        const SimpleMath::Vector3 RootToKneeVector{ CurrentWorldPositions[1] - CurrentWorldPositions[0] };
        const SimpleMath::Vector3 KneeProjectionOnRootToEndDirection{ RootToEndDirection * RootToKneeVector.Dot(RootToEndDirection) };
        const SimpleMath::Vector3 KneeOffsetFromRootToEndDirection{ RootToKneeVector - KneeProjectionOnRootToEndDirection };
        if (TryResolveNormalizedVector(KneeOffsetFromRootToEndDirection, OutPreferredBendDirection) == true) {
            return true;
        }

        SimpleMath::Vector3 LegPlaneNormal{ ResolveCrossProduct(CurrentWorldPositions[1] - CurrentWorldPositions[0], CurrentWorldPositions[2] - CurrentWorldPositions[1]) };
        if (TryResolveNormalizedVector(LegPlaneNormal, LegPlaneNormal) == false) {
            return false;
        }

        const SimpleMath::Vector3 FallbackPreferredBendDirection{ ResolveCrossProduct(LegPlaneNormal, RootToEndDirection) };
        return TryResolveNormalizedVector(FallbackPreferredBendDirection, OutPreferredBendDirection);
    }

    bool TrySolveLegWithIK(Arche::World& World, const Arche::EntityID ThighEntityId, const Arche::EntityID ShinEntityId, const Arche::EntityID FootEntityId, const float TargetOffsetY, const Game::IFootIKSolver& FootIKSolver, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        if (ThighEntityId == Arche::NullEntityID || ShinEntityId == Arche::NullEntityID || FootEntityId == Arche::NullEntityID || IsFiniteFloat(TargetOffsetY) == false) {
            return false;
        }

        if (::std::abs(TargetOffsetY) <= FootOffsetEpsilon) {
            return false;
        }

        const ::std::array<Arche::EntityID, 3> ChainEntityIds{ { ThighEntityId, ShinEntityId, FootEntityId } };
        ::std::array<SimpleMath::Matrix, 3> CurrentWorldMatrices{};
        for (::std::size_t JointIndex{}; JointIndex < ChainEntityIds.size(); ++JointIndex) {
            if (TryResolveWorldMatrix(World, ChainEntityIds[JointIndex], InOutWorldMatrices, CurrentWorldMatrices[JointIndex]) == false) {
                return false;
            }
        }

        ::std::array<SimpleMath::Vector3, 3> CurrentWorldPositions{};
        ::std::array<SimpleMath::Quaternion, 3> CurrentWorldRotations{};
        for (::std::size_t JointIndex{}; JointIndex < CurrentWorldMatrices.size(); ++JointIndex) {
            SimpleMath::Vector3 CurrentScale{};
            SimpleMath::Quaternion CurrentRotation{};
            SimpleMath::Vector3 CurrentPosition{};
            const bool IsCurrentDecomposeSucceeded{ CurrentWorldMatrices[JointIndex].Decompose(CurrentScale, CurrentRotation, CurrentPosition) };
            if (IsCurrentDecomposeSucceeded == false || IsFiniteVector3(CurrentScale) == false || IsFiniteVector3(CurrentPosition) == false || IsFiniteQuaternion(CurrentRotation) == false) {
                return false;
            }

            if (TryResolveNormalizedQuaternion(CurrentRotation, CurrentRotation) == false) {
                return false;
            }

            CurrentWorldPositions[JointIndex] = CurrentPosition;
            CurrentWorldRotations[JointIndex] = CurrentRotation;
        }

        SimpleMath::Vector3 TargetFootPosition{ CurrentWorldPositions.back() };
        TargetFootPosition.y += TargetOffsetY;
        if (IsFiniteVector3(TargetFootPosition) == false) {
            return false;
        }

        SimpleMath::Vector3 KneePreferredBendDirection{};
        bool IsKneePreferredBendDirectionResolved{ TryResolveKneePreferredBendDirection(CurrentWorldPositions, KneePreferredBendDirection) };
        if (IsKneePreferredBendDirectionResolved == false) {
            const SimpleMath::Vector3 FallbackKneePreferredBendDirection{ SimpleMath::Vector3::Transform(SimpleMath::Vector3::Right, CurrentWorldRotations[0]) };
            IsKneePreferredBendDirectionResolved = TryResolveNormalizedVector(FallbackKneePreferredBendDirection, KneePreferredBendDirection);
        }

        ::std::array<Game::FootIKJointConstraint, 1> JointConstraints{};
        JointConstraints[0].mJointIndex = KneeJointIndex;
        JointConstraints[0].mMinimumAngleRadians = KneeMinimumAngleRadians;
        JointConstraints[0].mMaximumAngleRadians = KneeMaximumAngleRadians;
        JointConstraints[0].mUseAngleLimit = true;
        if (IsKneePreferredBendDirectionResolved == true) {
            JointConstraints[0].mPreferredBendDirection = KneePreferredBendDirection;
            JointConstraints[0].mUsePreferredBendDirection = true;
        }

        Game::FootIKSolveParameters SolveParameters{};
        SolveParameters.mJointPositions = ::std::span<const SimpleMath::Vector3>{ CurrentWorldPositions };
        SolveParameters.mJointConstraints = ::std::span<const Game::FootIKJointConstraint>{ JointConstraints };
        SolveParameters.mTargetPosition = TargetFootPosition;
        SolveParameters.mMaxIterationCount = FootIKFabrikMaxIterationCount;
        SolveParameters.mConvergenceDistance = FootIKFabrikConvergenceDistance;

        Game::FootIKSolveResult SolveResult{};
        if (FootIKSolver.Solve(SolveParameters, SolveResult) == false || SolveResult.mJointPositions.size() != CurrentWorldPositions.size()) {
            return false;
        }

        bool IsAnyBoneUpdated{};
        for (::std::size_t JointIndex{}; JointIndex < ChainEntityIds.size(); ++JointIndex) {
            const SimpleMath::Vector3 DesiredWorldPosition{ SolveResult.mJointPositions[JointIndex] };
            if (IsFiniteVector3(DesiredWorldPosition) == false) {
                continue;
            }

            SimpleMath::Quaternion DesiredWorldRotation{ CurrentWorldRotations[JointIndex] };
            if (JointIndex + 1 < ChainEntityIds.size()) {
                const SimpleMath::Vector3 CurrentDirection{ CurrentWorldPositions[JointIndex + 1] - CurrentWorldPositions[JointIndex] };
                const SimpleMath::Vector3 DesiredDirection{ SolveResult.mJointPositions[JointIndex + 1] - SolveResult.mJointPositions[JointIndex] };
                SimpleMath::Quaternion DirectionDeltaRotation{};
                if (TryResolveFromToRotation(CurrentDirection, DesiredDirection, DirectionDeltaRotation) == true) {
                    SimpleMath::Quaternion RotatedWorldRotation{};
                    if (TryResolveWorldRotationWithWorldDelta(CurrentWorldRotations[JointIndex], DirectionDeltaRotation, RotatedWorldRotation) == true) {
                        DesiredWorldRotation = RotatedWorldRotation;
                    }
                }
            }

            const bool IsBoneUpdated{ TryApplyWorldTransformToBoneTransform(World, ChainEntityIds[JointIndex], DesiredWorldPosition, DesiredWorldRotation, InOutWorldMatrices) };
            if (IsBoneUpdated == true) {
                IsAnyBoneUpdated = true;
                InOutWorldMatrices.clear();
            }
        }

        return IsAnyBoneUpdated;
    }

    struct FootSurfaceAlignmentData final {
        SimpleMath::Vector3 FootWorldPosition{};
        SimpleMath::Vector3 SafeSurfaceNormal{};
        SimpleMath::Vector3 SafeFootToToeDirection{};
        SimpleMath::Vector3 CurrentFootNormal{};
        SimpleMath::Quaternion CurrentWorldRotation{};
    };

    bool TryResolveFootSurfaceAlignmentData(Arche::World& World, const Arche::EntityID FootEntityId, const Arche::EntityID ToeEntityId, const SimpleMath::Vector3& SurfaceNormal, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, FootSurfaceAlignmentData& OutFootSurfaceAlignmentData) {
        if (FootEntityId == Arche::NullEntityID || ToeEntityId == Arche::NullEntityID || IsFiniteVector3(SurfaceNormal) == false) {
            return false;
        }

        SimpleMath::Vector3 SafeSurfaceNormal{};
        if (TryResolveNormalizedVector(SurfaceNormal, SafeSurfaceNormal) == false) {
            return false;
        }

        SimpleMath::Matrix FootWorldMatrix{};
        SimpleMath::Matrix ToeWorldMatrix{};
        if (TryResolveWorldMatrix(World, FootEntityId, InOutWorldMatrices, FootWorldMatrix) == false || TryResolveWorldMatrix(World, ToeEntityId, InOutWorldMatrices, ToeWorldMatrix) == false) {
            return false;
        }

        const SimpleMath::Vector3 FootWorldPosition{ FootWorldMatrix._41, FootWorldMatrix._42, FootWorldMatrix._43 };
        const SimpleMath::Vector3 ToeWorldPosition{ ToeWorldMatrix._41, ToeWorldMatrix._42, ToeWorldMatrix._43 };
        if (IsFiniteVector3(FootWorldPosition) == false || IsFiniteVector3(ToeWorldPosition) == false) {
            return false;
        }

        SimpleMath::Vector3 CurrentWorldScale{};
        SimpleMath::Quaternion CurrentWorldRotation{};
        SimpleMath::Vector3 CurrentWorldPosition{};
        const bool IsCurrentFootDecomposeSucceeded{ FootWorldMatrix.Decompose(CurrentWorldScale, CurrentWorldRotation, CurrentWorldPosition) };
        if (IsCurrentFootDecomposeSucceeded == false || TryResolveNormalizedQuaternion(CurrentWorldRotation, CurrentWorldRotation) == false) {
            return false;
        }

        const SimpleMath::Vector3 FootToToeDirection{ ToeWorldPosition - FootWorldPosition };
        SimpleMath::Vector3 SafeFootToToeDirection{};
        if (TryResolveNormalizedVector(FootToToeDirection, SafeFootToToeDirection) == false) {
            return false;
        }

        const SimpleMath::Vector3 CurrentUpDirection{ SimpleMath::Vector3::Transform(SimpleMath::Vector3::Up, CurrentWorldRotation) };
        SimpleMath::Vector3 SafeCurrentUpDirection{};
        if (TryResolveNormalizedVector(CurrentUpDirection, SafeCurrentUpDirection) == false) {
            return false;
        }

        SimpleMath::Vector3 CurrentFootNormal{ SafeCurrentUpDirection - (SafeFootToToeDirection * SafeCurrentUpDirection.Dot(SafeFootToToeDirection)) };
        if (TryResolveNormalizedVector(CurrentFootNormal, CurrentFootNormal) == false) {
            CurrentFootNormal = SafeCurrentUpDirection;
        }

        if (TryResolveNormalizedVector(CurrentFootNormal, CurrentFootNormal) == false) {
            return false;
        }

        if (CurrentFootNormal.Dot(SafeSurfaceNormal) < 0.0f) {
            CurrentFootNormal *= -1.0f;
        }

        OutFootSurfaceAlignmentData.FootWorldPosition = FootWorldPosition;
        OutFootSurfaceAlignmentData.SafeSurfaceNormal = SafeSurfaceNormal;
        OutFootSurfaceAlignmentData.SafeFootToToeDirection = SafeFootToToeDirection;
        OutFootSurfaceAlignmentData.CurrentFootNormal = CurrentFootNormal;
        OutFootSurfaceAlignmentData.CurrentWorldRotation = CurrentWorldRotation;
        return true;
    }

    bool TryAlignFootToSurface(Arche::World& World, const Arche::EntityID FootEntityId, const Arche::EntityID ToeEntityId, const SimpleMath::Vector3& SurfaceNormal, const float AlignmentWeight, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        if (FootEntityId == Arche::NullEntityID || ToeEntityId == Arche::NullEntityID || IsFiniteVector3(SurfaceNormal) == false) {
            return false;
        }

        const float SafeAlignmentWeight{ IsFiniteFloat(AlignmentWeight) ? ::std::clamp(AlignmentWeight, 0.0f, 1.0f) : 0.0f };
        if (SafeAlignmentWeight <= FootPlantWeightEpsilon) {
            return false;
        }

        FootSurfaceAlignmentData FootSurfaceAlignmentDataValue{};
        if (TryResolveFootSurfaceAlignmentData(World, FootEntityId, ToeEntityId, SurfaceNormal, InOutWorldMatrices, FootSurfaceAlignmentDataValue) == false) {
            return false;
        }

        SimpleMath::Vector3 DesiredSurfaceNormal{ FootSurfaceAlignmentDataValue.CurrentFootNormal + ((FootSurfaceAlignmentDataValue.SafeSurfaceNormal - FootSurfaceAlignmentDataValue.CurrentFootNormal) * SafeAlignmentWeight) };
        if (TryResolveNormalizedVector(DesiredSurfaceNormal, DesiredSurfaceNormal) == false) {
            DesiredSurfaceNormal = FootSurfaceAlignmentDataValue.CurrentFootNormal;
        }

        SimpleMath::Quaternion SurfaceAlignDeltaRotation{};
        if (TryResolveClampedFromToRotation(FootSurfaceAlignmentDataValue.CurrentFootNormal, DesiredSurfaceNormal, MaxFootTiltRadians, SurfaceAlignDeltaRotation) == false) {
            return false;
        }

        SimpleMath::Quaternion DesiredWorldRotation{};
        if (TryResolveWorldRotationWithWorldDelta(FootSurfaceAlignmentDataValue.CurrentWorldRotation, SurfaceAlignDeltaRotation, DesiredWorldRotation) == false) {
            return false;
        }

        return TryApplyWorldRotationToBoneTransform(World, FootEntityId, DesiredWorldRotation, InOutWorldMatrices);
    }

    float ResolveSharedPelvisOffset(const float LeftOffset, const float RightOffset) {
        const float SafeLeftOffset{ IsFiniteFloat(LeftOffset) ? LeftOffset : 0.0f };
        const float SafeRightOffset{ IsFiniteFloat(RightOffset) ? RightOffset : 0.0f };
        if (SafeLeftOffset >= 0.0f && SafeRightOffset >= 0.0f) {
            const float MinimumPositiveOffset{ SafeLeftOffset < SafeRightOffset ? SafeLeftOffset : SafeRightOffset };
            return MinimumPositiveOffset * PelvisWeight;
        }

        if (SafeLeftOffset <= 0.0f && SafeRightOffset <= 0.0f) {
            const float MaximumNegativeOffset{ SafeLeftOffset > SafeRightOffset ? SafeLeftOffset : SafeRightOffset };
            return MaximumNegativeOffset * PelvisWeight;
        }

        return 0.0f;
    }

    float ResolveSmoothedOffset(const float CurrentOffset, const float TargetOffset, const float BlendSpeed, const float Dt) {
        const float SafeCurrentOffset{ IsFiniteFloat(CurrentOffset) ? CurrentOffset : 0.0f };
        const float SafeTargetOffset{ IsFiniteFloat(TargetOffset) ? TargetOffset : 0.0f };
        const float SafeBlendSpeed{ (::std::max)(BlendSpeed, 0.0f) };
        const float SafeDt{ (::std::max)(Dt, 0.0f) };
        if (SafeBlendSpeed <= 0.0f || SafeDt <= 0.0f) {
            return SafeTargetOffset;
        }

        const float BlendAlpha{ ::std::clamp(SafeBlendSpeed * SafeDt, 0.0f, 1.0f) };
        return ::std::lerp(SafeCurrentOffset, SafeTargetOffset, BlendAlpha);
    }

    float ResolveFootPlantWeight(const float TargetOffset) {
        const float SafeTargetOffset{ IsFiniteFloat(TargetOffset) ? TargetOffset : 0.0f };
        if (SafeTargetOffset <= FootPlantReleaseOffset) {
            return 0.0f;
        }

        if (SafeTargetOffset >= FootPlantEngageOffset) {
            return 1.0f;
        }

        const float PlantWeightAlpha{ (SafeTargetOffset - FootPlantReleaseOffset) / (FootPlantEngageOffset - FootPlantReleaseOffset) };
        return ::std::clamp(PlantWeightAlpha, 0.0f, 1.0f);
    }

}

namespace Game::IK {
    bool IsCachedBoneEntityValid(const Arche::World::WorldReadOnlyView& ReadOnlyWorld, const Arche::EntityID EntityId, const ::std::string_view ExpectedBoneNameText) {
        return ::IsCachedBoneEntityValid(ReadOnlyWorld, EntityId, ExpectedBoneNameText);
    }

    void ResolveFootBoneEntities(const Arche::World::WorldReadOnlyView& ReadOnlyWorld, const FootIKRig& FootIKRigComponent, const Arche::EntityID BoneRootEntityId, FootIKRuntime& InOutFootIKRuntimeComponent) {
        ::ResolveFootBoneEntities(ReadOnlyWorld, FootIKRigComponent, BoneRootEntityId, InOutFootIKRuntimeComponent);
    }

    bool TryResolveFootTargetOffset(Arche::World& World, const Arche::EntityID FootEntityId, ::std::unordered_map<Arche::EntityID, DirectX::SimpleMath::Matrix>& InOutWorldMatrices, float& OutTargetOffsetY, DirectX::SimpleMath::Vector3& OutGroundNormal) {
        return ::TryResolveFootTargetOffset(World, FootEntityId, InOutWorldMatrices, OutTargetOffsetY, OutGroundNormal);
    }

    bool TryResolveFootSurfaceNormals(Arche::World& World, const Arche::EntityID FootEntityId, const Arche::EntityID ToeEntityId, const DirectX::SimpleMath::Vector3& SurfaceNormal, ::std::unordered_map<Arche::EntityID, DirectX::SimpleMath::Matrix>& InOutWorldMatrices, DirectX::SimpleMath::Vector3& OutFootWorldPosition, DirectX::SimpleMath::Vector3& OutCurrentFootNormal, DirectX::SimpleMath::Vector3& OutSurfaceNormal) {
        FootSurfaceAlignmentData FootSurfaceAlignmentDataValue{};
        if (::TryResolveFootSurfaceAlignmentData(World, FootEntityId, ToeEntityId, SurfaceNormal, InOutWorldMatrices, FootSurfaceAlignmentDataValue) == false) {
            return false;
        }

        OutFootWorldPosition = FootSurfaceAlignmentDataValue.FootWorldPosition;
        OutCurrentFootNormal = FootSurfaceAlignmentDataValue.CurrentFootNormal;
        OutSurfaceNormal = FootSurfaceAlignmentDataValue.SafeSurfaceNormal;
        return true;
    }

    bool TryApplyOffsetToBoneTransform(Arche::World& World, const Arche::EntityID BoneEntityId, const float OffsetY, ::std::unordered_map<Arche::EntityID, DirectX::SimpleMath::Matrix>& InOutWorldMatrices) {
        return ::TryApplyOffsetToBoneTransform(World, BoneEntityId, OffsetY, InOutWorldMatrices);
    }

    bool TrySolveLegWithIK(Arche::World& World, const Arche::EntityID ThighEntityId, const Arche::EntityID ShinEntityId, const Arche::EntityID FootEntityId, const float TargetOffsetY, const IFootIKSolver& FootIKSolver, ::std::unordered_map<Arche::EntityID, DirectX::SimpleMath::Matrix>& InOutWorldMatrices) {
        return ::TrySolveLegWithIK(World, ThighEntityId, ShinEntityId, FootEntityId, TargetOffsetY, FootIKSolver, InOutWorldMatrices);
    }

    bool TryAlignFootToSurface(Arche::World& World, const Arche::EntityID FootEntityId, const Arche::EntityID ToeEntityId, const DirectX::SimpleMath::Vector3& SurfaceNormal, const float AlignmentWeight, ::std::unordered_map<Arche::EntityID, DirectX::SimpleMath::Matrix>& InOutWorldMatrices) {
        return ::TryAlignFootToSurface(World, FootEntityId, ToeEntityId, SurfaceNormal, AlignmentWeight, InOutWorldMatrices);
    }

    float ResolveSharedPelvisOffset(const float LeftOffset, const float RightOffset) {
        return ::ResolveSharedPelvisOffset(LeftOffset, RightOffset);
    }

    float ResolveSmoothedOffset(const float CurrentOffset, const float TargetOffset, const float BlendSpeed, const float Dt) {
        return ::ResolveSmoothedOffset(CurrentOffset, TargetOffset, BlendSpeed, Dt);
    }

    float ResolveFootPlantWeight(const float TargetOffset) {
        return ::ResolveFootPlantWeight(TargetOffset);
    }

}
