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

namespace {
    constexpr float FootOffsetEpsilon{ 1.0e-4f };
    constexpr float LegLengthEpsilon{ 1.0e-5f };
    constexpr float SurfaceNormalLengthEpsilon{ 1.0e-6f };
    constexpr float FootPlantWeightEpsilon{ 1.0e-3f };
    constexpr float FootPlantReleaseOffset{ -0.08f };
    constexpr float FootPlantEngageOffset{ 0.01f };
    constexpr float MaxFootTiltRadians{ 0.6108652382f };
    constexpr float ChainPropagationMinLimit{ 0.015f };
    constexpr float ChainPropagationMaxLimit{ 0.060f };
    constexpr float ChainPropagationRatio{ 0.18f };
    constexpr float ChainThighWeight{ 0.25f };
    constexpr float ChainShinWeight{ 0.35f };
    constexpr float ChainFootWeight{ 0.40f };
    constexpr float PelvisWeight{ 0.50f };
    constexpr float OverflowPelvisShare{ 1.0f / 3.0f };
    constexpr float OverflowThighWeight{ 1.0f };
    constexpr float OverflowShinWeight{ 1.0f };
    constexpr float OverflowFootWeight{ 2.0f };

    struct BoneOffsetTarget final {
        Arche::EntityID mEntityId{ Arche::NullEntityID };
        float mWeight{};
    };

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

        const float SafeMaxRotationRadians{ ::std::max(MaxRotationRadians, 0.0f) };
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

    bool TryResolveWorldRotationWithWorldDelta(const SimpleMath::Quaternion& CurrentWorldRotation, const SimpleMath::Quaternion& WorldDeltaRotation, const SimpleMath::Vector3& CurrentForwardDirection, SimpleMath::Quaternion& OutWorldRotation) {
        SimpleMath::Quaternion SafeCurrentWorldRotation{};
        SimpleMath::Quaternion SafeWorldDeltaRotation{};
        SimpleMath::Vector3 SafeCurrentForwardDirection{};
        if (TryResolveNormalizedQuaternion(CurrentWorldRotation, SafeCurrentWorldRotation) == false || TryResolveNormalizedQuaternion(WorldDeltaRotation, SafeWorldDeltaRotation) == false || TryResolveNormalizedVector(CurrentForwardDirection, SafeCurrentForwardDirection) == false) {
            return false;
        }

        SimpleMath::Quaternion InverseCurrentWorldRotation{ -SafeCurrentWorldRotation.x, -SafeCurrentWorldRotation.y, -SafeCurrentWorldRotation.z, SafeCurrentWorldRotation.w };
        if (TryResolveNormalizedQuaternion(InverseCurrentWorldRotation, InverseCurrentWorldRotation) == false) {
            return false;
        }

        const SimpleMath::Vector3 LocalForwardDirection{ SimpleMath::Vector3::Transform(SafeCurrentForwardDirection, InverseCurrentWorldRotation) };
        SimpleMath::Vector3 SafeLocalForwardDirection{};
        if (TryResolveNormalizedVector(LocalForwardDirection, SafeLocalForwardDirection) == false) {
            return false;
        }

        const SimpleMath::Quaternion WorldDeltaPreMultipliedRotation{ SafeWorldDeltaRotation * SafeCurrentWorldRotation };
        const SimpleMath::Quaternion WorldDeltaPostMultipliedRotation{ SafeCurrentWorldRotation * SafeWorldDeltaRotation };
        SimpleMath::Quaternion SafeWorldDeltaPreMultipliedRotation{};
        SimpleMath::Quaternion SafeWorldDeltaPostMultipliedRotation{};
        if (TryResolveNormalizedQuaternion(WorldDeltaPreMultipliedRotation, SafeWorldDeltaPreMultipliedRotation) == false || TryResolveNormalizedQuaternion(WorldDeltaPostMultipliedRotation, SafeWorldDeltaPostMultipliedRotation) == false) {
            return false;
        }

        const SimpleMath::Vector3 ExpectedForwardDirection{ SimpleMath::Vector3::Transform(SafeCurrentForwardDirection, SafeWorldDeltaRotation) };
        SimpleMath::Vector3 SafeExpectedForwardDirection{};
        if (TryResolveNormalizedVector(ExpectedForwardDirection, SafeExpectedForwardDirection) == false) {
            return false;
        }

        const SimpleMath::Vector3 PredictedForwardDirectionForPreMultipliedRotation{ SimpleMath::Vector3::Transform(SafeLocalForwardDirection, SafeWorldDeltaPreMultipliedRotation) };
        const SimpleMath::Vector3 PredictedForwardDirectionForPostMultipliedRotation{ SimpleMath::Vector3::Transform(SafeLocalForwardDirection, SafeWorldDeltaPostMultipliedRotation) };
        SimpleMath::Vector3 SafePredictedForwardDirectionForPreMultipliedRotation{};
        SimpleMath::Vector3 SafePredictedForwardDirectionForPostMultipliedRotation{};
        if (TryResolveNormalizedVector(PredictedForwardDirectionForPreMultipliedRotation, SafePredictedForwardDirectionForPreMultipliedRotation) == false || TryResolveNormalizedVector(PredictedForwardDirectionForPostMultipliedRotation, SafePredictedForwardDirectionForPostMultipliedRotation) == false) {
            return false;
        }

        const float PreMultipliedScore{ SafePredictedForwardDirectionForPreMultipliedRotation.Dot(SafeExpectedForwardDirection) };
        const float PostMultipliedScore{ SafePredictedForwardDirectionForPostMultipliedRotation.Dot(SafeExpectedForwardDirection) };
        OutWorldRotation = PreMultipliedScore >= PostMultipliedScore ? SafeWorldDeltaPreMultipliedRotation : SafeWorldDeltaPostMultipliedRotation;
        return true;
    }

    float ResolveSafeLength(const SimpleMath::Vector3& StartPosition, const SimpleMath::Vector3& EndPosition) {
        const float SegmentLength{ (EndPosition - StartPosition).Length() };
        if (IsFiniteFloat(SegmentLength) == false || SegmentLength <= LegLengthEpsilon) {
            return 0.0f;
        }

        return SegmentLength;
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

    bool TryApplyOffsetToBoneTransform(Arche::World& World, const Arche::EntityID BoneEntityId, const float OffsetY, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        if (BoneEntityId == Arche::NullEntityID || IsFiniteFloat(OffsetY) == false) {
            return false;
        }

        const float SafeOffsetY{ ::std::abs(OffsetY) <= FootOffsetEpsilon ? 0.0f : OffsetY };
        if (SafeOffsetY == 0.0f) {
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

        SimpleMath::Matrix ParentWorldMatrix{ SimpleMath::Matrix::Identity };
        if (BoneHierarchyComponent->parent != Arche::NullEntityID) {
            if (TryResolveWorldMatrix(World, BoneHierarchyComponent->parent, InOutWorldMatrices, ParentWorldMatrix) == false) {
                return false;
            }
        }

        SimpleMath::Matrix DesiredBoneWorldMatrix{ BoneWorldMatrix };
        DesiredBoneWorldMatrix._42 += SafeOffsetY;

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
        const bool IsDecomposeSucceeded{ DesiredTrsMatrix.Decompose(DecomposedScale, DecomposedRotation, DecomposedPosition) };
        if (IsDecomposeSucceeded == false) {
            return false;
        }

        if (IsFiniteVector3(DecomposedPosition) == false) {
            return false;
        }

        BoneTransformComponent->position = DecomposedPosition;
        InOutWorldMatrices[BoneEntityId] = DesiredBoneWorldMatrix;
        return true;
    }

    bool TryApplyWorldRotationToBoneTransform(Arche::World& World, const Arche::EntityID BoneEntityId, const SimpleMath::Quaternion& DesiredWorldRotation, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        if (BoneEntityId == Arche::NullEntityID || IsFiniteQuaternion(DesiredWorldRotation) == false) {
            return false;
        }

        SimpleMath::Quaternion SafeDesiredWorldRotation{ DesiredWorldRotation };
        const float DesiredWorldRotationLengthSquared{ (SafeDesiredWorldRotation.x * SafeDesiredWorldRotation.x) + (SafeDesiredWorldRotation.y * SafeDesiredWorldRotation.y) + (SafeDesiredWorldRotation.z * SafeDesiredWorldRotation.z) + (SafeDesiredWorldRotation.w * SafeDesiredWorldRotation.w) };
        if (IsFiniteFloat(DesiredWorldRotationLengthSquared) == false || DesiredWorldRotationLengthSquared <= SurfaceNormalLengthEpsilon) {
            return false;
        }

        SafeDesiredWorldRotation.Normalize();
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
        if (IsCurrentWorldDecomposeSucceeded == false || IsFiniteVector3(CurrentWorldScale) == false || IsFiniteVector3(CurrentWorldPosition) == false) {
            return false;
        }

        SimpleMath::Matrix ParentWorldMatrix{ SimpleMath::Matrix::Identity };
        if (BoneHierarchyComponent->parent != Arche::NullEntityID) {
            if (TryResolveWorldMatrix(World, BoneHierarchyComponent->parent, InOutWorldMatrices, ParentWorldMatrix) == false) {
                return false;
            }
        }

        const SimpleMath::Matrix DesiredBoneWorldMatrix{ SimpleMath::Matrix::CreateScale(CurrentWorldScale) * SimpleMath::Matrix::CreateFromQuaternion(SafeDesiredWorldRotation) * SimpleMath::Matrix::CreateTranslation(CurrentWorldPosition) };
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

    bool TryAlignFootToSurface(Arche::World& World, const Arche::EntityID FootEntityId, const Arche::EntityID ToeEntityId, const SimpleMath::Vector3& SurfaceNormal, const float AlignmentWeight, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        if (FootEntityId == Arche::NullEntityID || ToeEntityId == Arche::NullEntityID || IsFiniteVector3(SurfaceNormal) == false) {
            return false;
        }

        const float SafeAlignmentWeight{ IsFiniteFloat(AlignmentWeight) ? ::std::clamp(AlignmentWeight, 0.0f, 1.0f) : 0.0f };
        if (SafeAlignmentWeight <= FootPlantWeightEpsilon) {
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

        SimpleMath::Vector3 DesiredSurfaceNormal{ CurrentFootNormal + ((SafeSurfaceNormal - CurrentFootNormal) * SafeAlignmentWeight) };
        if (TryResolveNormalizedVector(DesiredSurfaceNormal, DesiredSurfaceNormal) == false) {
            DesiredSurfaceNormal = CurrentFootNormal;
        }

        SimpleMath::Quaternion SurfaceAlignDeltaRotation{};
        if (TryResolveClampedFromToRotation(CurrentFootNormal, DesiredSurfaceNormal, MaxFootTiltRadians, SurfaceAlignDeltaRotation) == false) {
            return false;
        }

        SimpleMath::Quaternion DesiredWorldRotation{};
        if (TryResolveWorldRotationWithWorldDelta(CurrentWorldRotation, SurfaceAlignDeltaRotation, SafeFootToToeDirection, DesiredWorldRotation) == false) {
            return false;
        }

        return TryApplyWorldRotationToBoneTransform(World, FootEntityId, DesiredWorldRotation, InOutWorldMatrices);
    }

    float ResolveLegChainPropagationLimit(Arche::World& World, const Arche::EntityID ThighEntityId, const Arche::EntityID ShinEntityId, const Arche::EntityID FootEntityId, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        if (ThighEntityId == Arche::NullEntityID || ShinEntityId == Arche::NullEntityID || FootEntityId == Arche::NullEntityID) {
            return ChainPropagationMinLimit;
        }

        SimpleMath::Matrix ThighWorldMatrix{};
        SimpleMath::Matrix ShinWorldMatrix{};
        SimpleMath::Matrix FootWorldMatrix{};
        if (TryResolveWorldMatrix(World, ThighEntityId, InOutWorldMatrices, ThighWorldMatrix) == false || TryResolveWorldMatrix(World, ShinEntityId, InOutWorldMatrices, ShinWorldMatrix) == false || TryResolveWorldMatrix(World, FootEntityId, InOutWorldMatrices, FootWorldMatrix) == false) {
            return ChainPropagationMinLimit;
        }

        const SimpleMath::Vector3 ThighWorldPosition{ ThighWorldMatrix._41, ThighWorldMatrix._42, ThighWorldMatrix._43 };
        const SimpleMath::Vector3 ShinWorldPosition{ ShinWorldMatrix._41, ShinWorldMatrix._42, ShinWorldMatrix._43 };
        const SimpleMath::Vector3 FootWorldPosition{ FootWorldMatrix._41, FootWorldMatrix._42, FootWorldMatrix._43 };
        if (IsFiniteVector3(ThighWorldPosition) == false || IsFiniteVector3(ShinWorldPosition) == false || IsFiniteVector3(FootWorldPosition) == false) {
            return ChainPropagationMinLimit;
        }

        const float ThighLength{ ResolveSafeLength(ThighWorldPosition, ShinWorldPosition) };
        const float ShinLength{ ResolveSafeLength(ShinWorldPosition, FootWorldPosition) };
        if (ThighLength <= 0.0f || ShinLength <= 0.0f) {
            return ChainPropagationMinLimit;
        }

        const float MinimumSegmentLength{ ThighLength < ShinLength ? ThighLength : ShinLength };
        const float PropagationLimit{ MinimumSegmentLength * ChainPropagationRatio };
        return ::std::clamp(PropagationLimit, ChainPropagationMinLimit, ChainPropagationMaxLimit);
    }

    void TryApplyDistributedOffsetToBones(Arche::World& World, const ::std::span<const BoneOffsetTarget> TargetBones, const float OffsetY, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        if (IsFiniteFloat(OffsetY) == false || ::std::abs(OffsetY) <= FootOffsetEpsilon) {
            return;
        }

        float TotalWeight{};
        for (const BoneOffsetTarget& TargetBone : TargetBones) {
            if (TargetBone.mEntityId == Arche::NullEntityID || TargetBone.mWeight <= 0.0f) {
                continue;
            }

            TotalWeight += TargetBone.mWeight;
        }

        if (TotalWeight <= 0.0f) {
            return;
        }

        for (const BoneOffsetTarget& TargetBone : TargetBones) {
            if (TargetBone.mEntityId == Arche::NullEntityID || TargetBone.mWeight <= 0.0f) {
                continue;
            }

            const float WeightedOffset{ OffsetY * (TargetBone.mWeight / TotalWeight) };
            const bool IsApplied{ TryApplyOffsetToBoneTransform(World, TargetBone.mEntityId, WeightedOffset, InOutWorldMatrices) };
            if (IsApplied == true) {
                InOutWorldMatrices.clear();
            }
        }
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

    float ResolveSharedOverflowPelvisOffset(const float LeftOverflowOffset, const float RightOverflowOffset) {
        const float SafeLeftOverflowOffset{ IsFiniteFloat(LeftOverflowOffset) ? LeftOverflowOffset : 0.0f };
        const float SafeRightOverflowOffset{ IsFiniteFloat(RightOverflowOffset) ? RightOverflowOffset : 0.0f };
        if (SafeLeftOverflowOffset >= 0.0f && SafeRightOverflowOffset >= 0.0f) {
            const float MinimumPositiveOverflowOffset{ SafeLeftOverflowOffset < SafeRightOverflowOffset ? SafeLeftOverflowOffset : SafeRightOverflowOffset };
            return MinimumPositiveOverflowOffset * OverflowPelvisShare;
        }

        if (SafeLeftOverflowOffset <= 0.0f && SafeRightOverflowOffset <= 0.0f) {
            const float MaximumNegativeOverflowOffset{ SafeLeftOverflowOffset > SafeRightOverflowOffset ? SafeLeftOverflowOffset : SafeRightOverflowOffset };
            return MaximumNegativeOverflowOffset * OverflowPelvisShare;
        }

        return 0.0f;
    }

    float ResolveSmoothedOffset(const float CurrentOffset, const float TargetOffset, const float BlendSpeed, const float Dt) {
        const float SafeCurrentOffset{ IsFiniteFloat(CurrentOffset) ? CurrentOffset : 0.0f };
        const float SafeTargetOffset{ IsFiniteFloat(TargetOffset) ? TargetOffset : 0.0f };
        const float SafeBlendSpeed{ ::std::max(BlendSpeed, 0.0f) };
        const float SafeDt{ ::std::max(Dt, 0.0f) };
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

    float ResolveDominantOffset(const float LeftOffset, const float RightOffset) {
        const float SafeLeftOffset{ IsFiniteFloat(LeftOffset) ? LeftOffset : 0.0f };
        const float SafeRightOffset{ IsFiniteFloat(RightOffset) ? RightOffset : 0.0f };
        return ::std::abs(SafeLeftOffset) >= ::std::abs(SafeRightOffset) ? SafeLeftOffset : SafeRightOffset;
    }
}

namespace Game {
    FootIKSystem::FootIKSystem()
        : mName{ "FootIKSystem" } {
    }

    FootIKSystem::~FootIKSystem() {
    }

    FootIKSystem::FootIKSystem(const FootIKSystem& Other)
        : mName{ Other.mName } {
    }

    FootIKSystem& FootIKSystem::operator=(const FootIKSystem& Other) {
        if (this == &Other) {
            return *this;
        }

        mName = Other.mName;
        return *this;
    }

    FootIKSystem::FootIKSystem(FootIKSystem&& Other) noexcept
        : mName{ ::std::move(Other.mName) } {
    }

    FootIKSystem& FootIKSystem::operator=(FootIKSystem&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mName = ::std::move(Other.mName);
        return *this;
    }

    const std::string& FootIKSystem::Name() const {
        return mName;
    }

    Phase FootIKSystem::GetPhase() const {
        return Phase::RenderPrepare;
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
        (void)Ctx;

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

            const bool IsLeftFootCachedEntityValid{ IsCachedBoneEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mLeftFootEntityId, LeftFootBoneNameText) };
            const bool IsRightFootCachedEntityValid{ IsCachedBoneEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mRightFootEntityId, RightFootBoneNameText) };
            const bool IsLeftToeCachedEntityValid{ IsCachedBoneEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mLeftToeEntityId, LeftToeBoneNameText) };
            const bool IsRightToeCachedEntityValid{ IsCachedBoneEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mRightToeEntityId, RightToeBoneNameText) };
            const bool IsLeftShinCachedEntityValid{ IsCachedBoneEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mLeftShinEntityId, LeftShinBoneNameText) };
            const bool IsRightShinCachedEntityValid{ IsCachedBoneEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mRightShinEntityId, RightShinBoneNameText) };
            const bool IsLeftThighCachedEntityValid{ IsCachedBoneEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mLeftThighEntityId, LeftThighBoneNameText) };
            const bool IsRightThighCachedEntityValid{ IsCachedBoneEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mRightThighEntityId, RightThighBoneNameText) };
            const bool IsPelvisCachedEntityValid{ IsCachedBoneEntityValid(ReadOnlyWorld, FootIKRuntimeComponent.mPelvisEntityId, PelvisBoneNameText) };
            if (IsLeftFootCachedEntityValid == false || IsRightFootCachedEntityValid == false || IsLeftToeCachedEntityValid == false || IsRightToeCachedEntityValid == false || IsLeftShinCachedEntityValid == false || IsRightShinCachedEntityValid == false || IsLeftThighCachedEntityValid == false || IsRightThighCachedEntityValid == false || IsPelvisCachedEntityValid == false) {
                FootIKRuntimeComponent.mResolved = false;
            }

            if (FootIKRuntimeComponent.mResolved == false) {
                ResolveFootBoneEntities(ReadOnlyWorld, FootIKRigComponent, BoneSkinReferenceComponent.boneRootEntityId, FootIKRuntimeComponent);
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
                const bool IsLeftTargetOffsetResolved{ TryResolveFootTargetOffset(World, FootIKRuntimeComponent.mLeftFootEntityId, WorldMatrices, LeftResolvedTargetOffset, LeftResolvedGroundNormal) };
                if (IsLeftTargetOffsetResolved == true) {
                    LeftRawTargetOffset = LeftResolvedTargetOffset;
                    LeftTargetPlantWeight = ResolveFootPlantWeight(LeftResolvedTargetOffset);
                    LeftGroundNormal = LeftResolvedGroundNormal;
                    IsLeftGroundNormalResolved = true;
                }

                float RightResolvedTargetOffset{};
                SimpleMath::Vector3 RightResolvedGroundNormal{ SimpleMath::Vector3::Up };
                const bool IsRightTargetOffsetResolved{ TryResolveFootTargetOffset(World, FootIKRuntimeComponent.mRightFootEntityId, WorldMatrices, RightResolvedTargetOffset, RightResolvedGroundNormal) };
                if (IsRightTargetOffsetResolved == true) {
                    RightRawTargetOffset = RightResolvedTargetOffset;
                    RightTargetPlantWeight = ResolveFootPlantWeight(RightResolvedTargetOffset);
                    RightGroundNormal = RightResolvedGroundNormal;
                    IsRightGroundNormalResolved = true;
                }
            }

            const float PreviousLeftPlantWeight{ IsFiniteFloat(FootIKRuntimeComponent.mLeftPlantWeight) ? ::std::clamp(FootIKRuntimeComponent.mLeftPlantWeight, 0.0f, 1.0f) : 0.0f };
            const float PreviousRightPlantWeight{ IsFiniteFloat(FootIKRuntimeComponent.mRightPlantWeight) ? ::std::clamp(FootIKRuntimeComponent.mRightPlantWeight, 0.0f, 1.0f) : 0.0f };
            const float SmoothedLeftPlantWeight{ ResolveSmoothedOffset(PreviousLeftPlantWeight, LeftTargetPlantWeight, FootIKRigComponent.mBlendSpeed, Dt) };
            const float SmoothedRightPlantWeight{ ResolveSmoothedOffset(PreviousRightPlantWeight, RightTargetPlantWeight, FootIKRigComponent.mBlendSpeed, Dt) };
            FootIKRuntimeComponent.mLeftPlantWeight = IsFiniteFloat(SmoothedLeftPlantWeight) ? ::std::clamp(SmoothedLeftPlantWeight, 0.0f, 1.0f) : 0.0f;
            FootIKRuntimeComponent.mRightPlantWeight = IsFiniteFloat(SmoothedRightPlantWeight) ? ::std::clamp(SmoothedRightPlantWeight, 0.0f, 1.0f) : 0.0f;

            const float MaxLift{ ::std::max(FootIKRigComponent.mMaxLift, 0.0f) };
            const float MaxDrop{ ::std::max(FootIKRigComponent.mMaxDrop, 0.0f) };
            float LeftTargetOffset{ ::std::clamp(LeftRawTargetOffset, -MaxDrop, MaxLift) };
            float RightTargetOffset{ ::std::clamp(RightRawTargetOffset, -MaxDrop, MaxLift) };
            LeftTargetOffset *= FootIKRuntimeComponent.mLeftPlantWeight;
            RightTargetOffset *= FootIKRuntimeComponent.mRightPlantWeight;

            const float PreviousLeftOffset{ IsFiniteFloat(FootIKRuntimeComponent.mLeftCurrentOffset) ? FootIKRuntimeComponent.mLeftCurrentOffset : 0.0f };
            const float PreviousRightOffset{ IsFiniteFloat(FootIKRuntimeComponent.mRightCurrentOffset) ? FootIKRuntimeComponent.mRightCurrentOffset : 0.0f };
            const float SmoothedLeftOffset{ ResolveSmoothedOffset(PreviousLeftOffset, LeftTargetOffset, FootIKRigComponent.mBlendSpeed, Dt) };
            const float SmoothedRightOffset{ ResolveSmoothedOffset(PreviousRightOffset, RightTargetOffset, FootIKRigComponent.mBlendSpeed, Dt) };
            FootIKRuntimeComponent.mLeftCurrentOffset = IsFiniteFloat(SmoothedLeftOffset) ? SmoothedLeftOffset : 0.0f;
            FootIKRuntimeComponent.mRightCurrentOffset = IsFiniteFloat(SmoothedRightOffset) ? SmoothedRightOffset : 0.0f;
            FootIKRuntimeComponent.mCurrentOffset = ResolveDominantOffset(FootIKRuntimeComponent.mLeftCurrentOffset, FootIKRuntimeComponent.mRightCurrentOffset);

            const float PelvisOffset{ FootIKRuntimeComponent.mPelvisEntityId == Arche::NullEntityID ? 0.0f : ResolveSharedPelvisOffset(FootIKRuntimeComponent.mLeftCurrentOffset, FootIKRuntimeComponent.mRightCurrentOffset) };
            if (FootIKRuntimeComponent.mPelvisEntityId != Arche::NullEntityID) {
                const bool IsPelvisOffsetApplied{ TryApplyOffsetToBoneTransform(World, FootIKRuntimeComponent.mPelvisEntityId, PelvisOffset, WorldMatrices) };
                if (IsPelvisOffsetApplied == true) {
                    WorldMatrices.clear();
                }
            }

            const float LeftLegOffset{ FootIKRuntimeComponent.mLeftCurrentOffset - PelvisOffset };
            const float RightLegOffset{ FootIKRuntimeComponent.mRightCurrentOffset - PelvisOffset };
            const float LeftChainLimit{ ResolveLegChainPropagationLimit(World, FootIKRuntimeComponent.mLeftThighEntityId, FootIKRuntimeComponent.mLeftShinEntityId, FootIKRuntimeComponent.mLeftFootEntityId, WorldMatrices) };
            const float RightChainLimit{ ResolveLegChainPropagationLimit(World, FootIKRuntimeComponent.mRightThighEntityId, FootIKRuntimeComponent.mRightShinEntityId, FootIKRuntimeComponent.mRightFootEntityId, WorldMatrices) };
            const float LeftChainOffset{ ::std::clamp(LeftLegOffset, -LeftChainLimit, LeftChainLimit) };
            const float RightChainOffset{ ::std::clamp(RightLegOffset, -RightChainLimit, RightChainLimit) };
            const float LeftOverflowOffset{ LeftLegOffset - LeftChainOffset };
            const float RightOverflowOffset{ RightLegOffset - RightChainOffset };
            const ::std::array<BoneOffsetTarget, 3> LeftLegChain{ { BoneOffsetTarget{ FootIKRuntimeComponent.mLeftThighEntityId, ChainThighWeight }, BoneOffsetTarget{ FootIKRuntimeComponent.mLeftShinEntityId, ChainShinWeight }, BoneOffsetTarget{ FootIKRuntimeComponent.mLeftFootEntityId, ChainFootWeight } } };
            const ::std::array<BoneOffsetTarget, 3> RightLegChain{ { BoneOffsetTarget{ FootIKRuntimeComponent.mRightThighEntityId, ChainThighWeight }, BoneOffsetTarget{ FootIKRuntimeComponent.mRightShinEntityId, ChainShinWeight }, BoneOffsetTarget{ FootIKRuntimeComponent.mRightFootEntityId, ChainFootWeight } } };
            TryApplyDistributedOffsetToBones(World, LeftLegChain, LeftChainOffset, WorldMatrices);
            TryApplyDistributedOffsetToBones(World, RightLegChain, RightChainOffset, WorldMatrices);

            const float SharedOverflowPelvisOffset{ FootIKRuntimeComponent.mPelvisEntityId == Arche::NullEntityID ? 0.0f : ResolveSharedOverflowPelvisOffset(LeftOverflowOffset, RightOverflowOffset) };
            if (FootIKRuntimeComponent.mPelvisEntityId != Arche::NullEntityID) {
                const bool IsOverflowPelvisApplied{ TryApplyOffsetToBoneTransform(World, FootIKRuntimeComponent.mPelvisEntityId, SharedOverflowPelvisOffset, WorldMatrices) };
                if (IsOverflowPelvisApplied == true) {
                    WorldMatrices.clear();
                }
            }

            const float LeftOverflowAfterPelvis{ LeftOverflowOffset - SharedOverflowPelvisOffset };
            const float RightOverflowAfterPelvis{ RightOverflowOffset - SharedOverflowPelvisOffset };
            const ::std::array<BoneOffsetTarget, 3> LeftOverflowTargets{ { BoneOffsetTarget{ FootIKRuntimeComponent.mLeftThighEntityId, OverflowThighWeight }, BoneOffsetTarget{ FootIKRuntimeComponent.mLeftShinEntityId, OverflowShinWeight }, BoneOffsetTarget{ FootIKRuntimeComponent.mLeftFootEntityId, OverflowFootWeight } } };
            const ::std::array<BoneOffsetTarget, 3> RightOverflowTargets{ { BoneOffsetTarget{ FootIKRuntimeComponent.mRightThighEntityId, OverflowThighWeight }, BoneOffsetTarget{ FootIKRuntimeComponent.mRightShinEntityId, OverflowShinWeight }, BoneOffsetTarget{ FootIKRuntimeComponent.mRightFootEntityId, OverflowFootWeight } } };
            TryApplyDistributedOffsetToBones(World, LeftOverflowTargets, LeftOverflowAfterPelvis, WorldMatrices);
            TryApplyDistributedOffsetToBones(World, RightOverflowTargets, RightOverflowAfterPelvis, WorldMatrices);

            if (FootIKRigComponent.mEnabled == true && FootIKRuntimeComponent.mResolved == true) {
                if (IsLeftGroundNormalResolved == true) {
                    const bool IsLeftFootSurfaceAligned{ TryAlignFootToSurface(World, FootIKRuntimeComponent.mLeftFootEntityId, FootIKRuntimeComponent.mLeftToeEntityId, LeftGroundNormal, FootIKRuntimeComponent.mLeftPlantWeight, WorldMatrices) };
                    if (IsLeftFootSurfaceAligned == true) {
                        WorldMatrices.clear();
                    }
                }

                if (IsRightGroundNormalResolved == true) {
                    const bool IsRightFootSurfaceAligned{ TryAlignFootToSurface(World, FootIKRuntimeComponent.mRightFootEntityId, FootIKRuntimeComponent.mRightToeEntityId, RightGroundNormal, FootIKRuntimeComponent.mRightPlantWeight, WorldMatrices) };
                    if (IsRightFootSurfaceAligned == true) {
                        WorldMatrices.clear();
                    }
                }
            }
        }
    }
}
