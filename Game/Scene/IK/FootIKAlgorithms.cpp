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
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/IK/FootIKSolver.h"
#include "Utility/MathValidation.h"

namespace Game::IK {
    constexpr float FootOffsetEpsilon{ 1.0e-4f };
    constexpr float SurfaceNormalLengthEpsilon{ 1.0e-6f };
    constexpr float FootRaycastStartOffset{ 0.3f };
    constexpr float FootRaycastLength{ 0.3f + 0.2f };
    constexpr float PelvisWeight{ 0.50f };
    constexpr int FootIKFabrikMaxIterationCount{ 12 };
    constexpr float FootIKFabrikConvergenceDistance{ 1.0e-3f };
    constexpr std::size_t KneeJointIndex{ 1 };
    constexpr float KneeMinimumAngleRadians{ 0.0872664626f };
    constexpr float KneeMaximumAngleRadians{ 3.0543261909f };
    constexpr float ToeContactCorrectionMaxRadians{ 0.5235987756f };

    SimpleMath::Vector3 ResolveCrossProduct(const SimpleMath::Vector3& Left, const SimpleMath::Vector3& Right) {
        return SimpleMath::Vector3{ (Left.y * Right.z) - (Left.z * Right.y), (Left.z * Right.x) - (Left.x * Right.z), (Left.x * Right.y) - (Left.y * Right.x) };
    }

    bool TryResolveNormalizedVector(const SimpleMath::Vector3& SourceVector, SimpleMath::Vector3& OutNormalizedVector) {
        if (MathUtility::IsFiniteVector3(SourceVector) == false) {
            return false;
        }

        const float SourceVectorLengthSquared{ SourceVector.LengthSquared() };
        if (MathUtility::IsFiniteFloat(SourceVectorLengthSquared) == false || SourceVectorLengthSquared <= SurfaceNormalLengthEpsilon) {
            return false;
        }

        OutNormalizedVector = SourceVector;
        OutNormalizedVector.Normalize();
        return MathUtility::IsFiniteVector3(OutNormalizedVector);
    }

    bool TryResolveNormalizedQuaternion(const SimpleMath::Quaternion& SourceQuaternion, SimpleMath::Quaternion& OutNormalizedQuaternion) {
        if (MathUtility::IsFiniteQuaternion(SourceQuaternion) == false) {
            return false;
        }

        const float SourceQuaternionLengthSquared{ (SourceQuaternion.x * SourceQuaternion.x) + (SourceQuaternion.y * SourceQuaternion.y) + (SourceQuaternion.z * SourceQuaternion.z) + (SourceQuaternion.w * SourceQuaternion.w) };
        if (MathUtility::IsFiniteFloat(SourceQuaternionLengthSquared) == false || SourceQuaternionLengthSquared <= SurfaceNormalLengthEpsilon) {
            return false;
        }

        OutNormalizedQuaternion = SourceQuaternion;
        OutNormalizedQuaternion.Normalize();
        return MathUtility::IsFiniteQuaternion(OutNormalizedQuaternion);
    }

    bool TryResolveFromToRotation(const SimpleMath::Vector3& SourceDirection, const SimpleMath::Vector3& TargetDirection, SimpleMath::Quaternion& OutRotation) {
        SimpleMath::Vector3 SafeSourceDirection{};
        SimpleMath::Vector3 SafeTargetDirection{};
        if (TryResolveNormalizedVector(SourceDirection, SafeSourceDirection) == false || TryResolveNormalizedVector(TargetDirection, SafeTargetDirection) == false) {
            return false;
        }

        const float DirectionDot{ std::clamp(SafeSourceDirection.Dot(SafeTargetDirection), -1.0f, 1.0f) };
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

        const float RotationAngleRadians{ std::acos(DirectionDot) };
        const SimpleMath::Quaternion AxisAngleRotation{ SimpleMath::Quaternion::CreateFromAxisAngle(SafeRotationAxis, RotationAngleRadians) };
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

    bool TryResolveRotatedPointAroundPivot(const SimpleMath::Vector3& SourcePoint, const SimpleMath::Vector3& PivotPoint, const SimpleMath::Quaternion& WorldDeltaRotation, SimpleMath::Vector3& OutRotatedPoint) {
        if (MathUtility::IsFiniteVector3(SourcePoint) == false || MathUtility::IsFiniteVector3(PivotPoint) == false) {
            return false;
        }

        SimpleMath::Quaternion SafeWorldDeltaRotation{};
        if (TryResolveNormalizedQuaternion(WorldDeltaRotation, SafeWorldDeltaRotation) == false) {
            return false;
        }

        const SimpleMath::Vector3 PivotToSource{ SourcePoint - PivotPoint };
        if (MathUtility::IsFiniteVector3(PivotToSource) == false) {
            return false;
        }

        const SimpleMath::Vector3 RotatedPivotToSource{ SimpleMath::Vector3::Transform(PivotToSource, SafeWorldDeltaRotation) };
        if (MathUtility::IsFiniteVector3(RotatedPivotToSource) == false) {
            return false;
        }

        OutRotatedPoint = PivotPoint + RotatedPivotToSource;
        return MathUtility::IsFiniteVector3(OutRotatedPoint);
    }

    SimpleMath::Matrix BuildLocalWorldMatrix(const Game::Transform& TransformComponent) {
        const SimpleMath::Matrix TrsMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
        return TransformComponent.nodeToParent * TrsMatrix;
    }

    bool TryResolveWorldMatrix(Arche::World& World, const Arche::EntityID EntityId, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, SimpleMath::Matrix& OutWorldMatrix) {
        const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CachedWorldMatrixIter{ InOutWorldMatrices.find(EntityId) };
        if (CachedWorldMatrixIter != InOutWorldMatrices.end()) {
            OutWorldMatrix = CachedWorldMatrixIter->second;
            return true;
        }

        std::vector<Arche::EntityID> EntityPath{};
        Arche::EntityID CurrentEntityId{ EntityId };
        SimpleMath::Matrix ParentWorldMatrix{ SimpleMath::Matrix::Identity };
        while (CurrentEntityId != Arche::NullEntityID) {
            const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CurrentCachedWorldMatrixIter{ InOutWorldMatrices.find(CurrentEntityId) };
            if (CurrentCachedWorldMatrixIter != InOutWorldMatrices.end()) {
                ParentWorldMatrix = CurrentCachedWorldMatrixIter->second;
                break;
            }

            const Game::Transform* TransformComponent{ std::as_const(World).GetComponent<Game::Transform>(CurrentEntityId) };
            const Game::EntityHierarchy* HierarchyComponent{ std::as_const(World).GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (TransformComponent == nullptr || HierarchyComponent == nullptr) {
                return false;
            }

            EntityPath.push_back(CurrentEntityId);
            CurrentEntityId = HierarchyComponent->parent;
        }

        for (std::vector<Arche::EntityID>::const_reverse_iterator EntityPathIter{ EntityPath.crbegin() }; EntityPathIter != EntityPath.crend(); ++EntityPathIter) {
            const Arche::EntityID CurrentPathEntityId{ *EntityPathIter };
            const Game::Transform* TransformComponent{ std::as_const(World).GetComponent<Game::Transform>(CurrentPathEntityId) };
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

    Arche::EntityID FindBoneEntityByNameInHierarchy(const Arche::World::WorldReadOnlyView& ReadOnlyWorld, const Arche::EntityID RootEntityId, const std::string_view TargetNameText) {
        if (RootEntityId == Arche::NullEntityID || TargetNameText.empty() == true) {
            return Arche::NullEntityID;
        }

        std::vector<Arche::EntityID> Stack{};
        Stack.reserve(256);
        Stack.push_back(RootEntityId);
        while (Stack.empty() == false) {
            const Arche::EntityID CurrentEntityId{ Stack.back() };
            Stack.pop_back();

            const Game::Bone* BoneComponent{ ReadOnlyWorld.GetComponent<Game::Bone>(CurrentEntityId) };
            const Game::Name* NameComponent{ ReadOnlyWorld.GetComponent<Game::Name>(CurrentEntityId) };
            if (BoneComponent != nullptr && NameComponent != nullptr) {
                const std::string_view CurrentNameText{ Game::GetNameTextView(*NameComponent) };
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

    bool IsCachedBoneEntityValid(const Arche::World::WorldReadOnlyView& ReadOnlyWorld, const Arche::EntityID EntityId, const std::string_view ExpectedBoneNameText) {
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

        const std::string_view CurrentNameText{ Game::GetNameTextView(*NameComponent) };
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

        const std::string_view LeftFootBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mLeftFootBoneName) };
        const std::string_view RightFootBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mRightFootBoneName) };
        const std::string_view LeftToeBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mLeftToeBoneName) };
        const std::string_view RightToeBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mRightToeBoneName) };
        const std::string_view LeftShinBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mLeftShinBoneName) };
        const std::string_view RightShinBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mRightShinBoneName) };
        const std::string_view LeftThighBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mLeftThighBoneName) };
        const std::string_view RightThighBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mRightThighBoneName) };
        const std::string_view PelvisBoneNameText{ Game::GetFootIKRigBoneNameText(FootIKRigComponent.mPelvisBoneName) };

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

    bool TryResolveWorldObb(Arche::World& World, const Arche::EntityID EntityId, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, DirectX::BoundingOrientedBox& OutWorldObb) {
        if (EntityId == Arche::NullEntityID) {
            return false;
        }

        const Game::BoundingBox* BoundingBoxComponent{ std::as_const(World).GetComponent<Game::BoundingBox>(EntityId) };
        if (BoundingBoxComponent == nullptr) {
            return false;
        }

        SimpleMath::Matrix WorldMatrix{};
        if (TryResolveWorldMatrix(World, EntityId, InOutWorldMatrices, WorldMatrix) == false) {
            return false;
        }

        BoundingBoxComponent->GetObb().Transform(OutWorldObb, WorldMatrix);
        return true;
    }

    bool TryResolveNearestTerrainRaycastHit(const Terrain::ITerrainQuery& TerrainQuery, const SimpleMath::Ray& Ray, const float RayLength, SimpleMath::Vector3& OutHitPoint, SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) {
        if (MathUtility::IsFiniteVector3(Ray.position) == false || MathUtility::IsFiniteVector3(Ray.direction) == false || MathUtility::IsFiniteFloat(RayLength) == false || RayLength <= 0.0f) {
            return false;
        }

        SimpleMath::Vector3 HitPoint{};
        SimpleMath::Vector3 HitNormal{ SimpleMath::Vector3::Up };
        float HitDistance{};
        const bool IsHit{ TerrainQuery.TryRaycast(Ray, RayLength, HitPoint, HitNormal, HitDistance) };
        if (IsHit == false || MathUtility::IsFiniteVector3(HitPoint) == false || MathUtility::IsFiniteVector3(HitNormal) == false || MathUtility::IsFiniteFloat(HitDistance) == false || HitDistance < 0.0f || HitDistance > RayLength) {
            return false;
        }

        SimpleMath::Vector3 SafeHitNormal{};
        if (TryResolveNormalizedVector(HitNormal, SafeHitNormal) == false) {
            return false;
        }

        OutHitPoint = HitPoint;
        OutHitNormal = SafeHitNormal;
        OutHitDistance = HitDistance;
        return true;
    }

    bool CreateMerged(DirectX::BoundingOrientedBox& OutMergedWorldObb, const DirectX::BoundingOrientedBox& LeftWorldObb, const DirectX::BoundingOrientedBox& RightWorldObb) {
        std::array<DirectX::XMFLOAT3, 8> LeftCorners{};
        std::array<DirectX::XMFLOAT3, 8> RightCorners{};
        LeftWorldObb.GetCorners(LeftCorners.data());
        RightWorldObb.GetCorners(RightCorners.data());

        std::array<DirectX::XMFLOAT3, 16> MergedCorners{};
        for (std::size_t CornerIndex{}; CornerIndex < LeftCorners.size(); ++CornerIndex) {
            MergedCorners[CornerIndex] = LeftCorners[CornerIndex];
            MergedCorners[CornerIndex + LeftCorners.size()] = RightCorners[CornerIndex];
        }

        DirectX::BoundingOrientedBox::CreateFromPoints(OutMergedWorldObb, MergedCorners.size(), MergedCorners.data(), sizeof(DirectX::XMFLOAT3));
        const SimpleMath::Vector3 MergedCenter{ OutMergedWorldObb.Center.x, OutMergedWorldObb.Center.y, OutMergedWorldObb.Center.z };
        const SimpleMath::Vector3 MergedExtents{ OutMergedWorldObb.Extents.x, OutMergedWorldObb.Extents.y, OutMergedWorldObb.Extents.z };
        const SimpleMath::Quaternion MergedOrientation{ OutMergedWorldObb.Orientation.x, OutMergedWorldObb.Orientation.y, OutMergedWorldObb.Orientation.z, OutMergedWorldObb.Orientation.w };
        return MathUtility::IsFiniteVector3(MergedCenter) == true && MathUtility::IsFiniteVector3(MergedExtents) == true && MathUtility::IsFiniteQuaternion(MergedOrientation) == true;
    }

    bool TryResolveObbVerticalCornerPairs(const DirectX::BoundingOrientedBox& WorldObb, std::array<SimpleMath::Vector3, 4>& OutBottomCorners, std::array<SimpleMath::Vector3, 4>& OutTopCorners) {
        const SimpleMath::Vector3 ObbCenter{ WorldObb.Center.x, WorldObb.Center.y, WorldObb.Center.z };
        const SimpleMath::Vector3 ObbExtents{ std::abs(WorldObb.Extents.x), std::abs(WorldObb.Extents.y), std::abs(WorldObb.Extents.z) };
        if (MathUtility::IsFiniteVector3(ObbCenter) == false || MathUtility::IsFiniteVector3(ObbExtents) == false) {
            return false;
        }

        SimpleMath::Quaternion ObbOrientation{ WorldObb.Orientation.x, WorldObb.Orientation.y, WorldObb.Orientation.z, WorldObb.Orientation.w };
        if (TryResolveNormalizedQuaternion(ObbOrientation, ObbOrientation) == false) {
            ObbOrientation = SimpleMath::Quaternion::Identity;
        }

        SimpleMath::Vector3 RightAxis{ SimpleMath::Vector3::Transform(SimpleMath::Vector3::Right, ObbOrientation) };
        SimpleMath::Vector3 UpAxis{ SimpleMath::Vector3::Transform(SimpleMath::Vector3::Up, ObbOrientation) };
        SimpleMath::Vector3 ForwardAxis{ SimpleMath::Vector3::Transform(SimpleMath::Vector3::Forward, ObbOrientation) };
        if (TryResolveNormalizedVector(RightAxis, RightAxis) == false || TryResolveNormalizedVector(UpAxis, UpAxis) == false || TryResolveNormalizedVector(ForwardAxis, ForwardAxis) == false) {
            return false;
        }

        const float UpAxisWorldUpDot{ UpAxis.Dot(SimpleMath::Vector3::Up) };
        if (MathUtility::IsFiniteFloat(UpAxisWorldUpDot) == false) {
            return false;
        }

        if (UpAxisWorldUpDot < 0.0f) {
            UpAxis *= -1.0f;
        }

        const SimpleMath::Vector3 RightOffset{ RightAxis * ObbExtents.x };
        const SimpleMath::Vector3 UpOffset{ UpAxis * ObbExtents.y };
        const SimpleMath::Vector3 ForwardOffset{ ForwardAxis * ObbExtents.z };
        const SimpleMath::Vector3 BottomFaceCenter{ ObbCenter - UpOffset };
        const SimpleMath::Vector3 TopFaceCenter{ ObbCenter + UpOffset };

        OutBottomCorners[0] = BottomFaceCenter - RightOffset - ForwardOffset;
        OutBottomCorners[1] = BottomFaceCenter + RightOffset - ForwardOffset;
        OutBottomCorners[2] = BottomFaceCenter + RightOffset + ForwardOffset;
        OutBottomCorners[3] = BottomFaceCenter - RightOffset + ForwardOffset;
        OutTopCorners[0] = TopFaceCenter - RightOffset - ForwardOffset;
        OutTopCorners[1] = TopFaceCenter + RightOffset - ForwardOffset;
        OutTopCorners[2] = TopFaceCenter + RightOffset + ForwardOffset;
        OutTopCorners[3] = TopFaceCenter - RightOffset + ForwardOffset;

        for (std::size_t CornerIndex{}; CornerIndex < OutBottomCorners.size(); ++CornerIndex) {
            if (MathUtility::IsFiniteVector3(OutBottomCorners[CornerIndex]) == false || MathUtility::IsFiniteVector3(OutTopCorners[CornerIndex]) == false) {
                return false;
            }
        }

        return true;
    }

    bool TryResolveFootObbAndToeObbCorners(Arche::World& World, const Arche::EntityID FootEntityId, const Arche::EntityID ToeEntityId, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, std::array<SimpleMath::Vector3, 4>& OutCornerPoints, std::array<SimpleMath::Vector3, 4>& OutCornerDirections) {
        DirectX::BoundingOrientedBox FootWorldObb{};
        DirectX::BoundingOrientedBox ToeWorldObb{};
        if (TryResolveWorldObb(World, FootEntityId, InOutWorldMatrices, FootWorldObb) == false || TryResolveWorldObb(World, ToeEntityId, InOutWorldMatrices, ToeWorldObb) == false) {
            return false;
        }

        DirectX::BoundingOrientedBox MergedWorldObb{};
        if (CreateMerged(MergedWorldObb, FootWorldObb, ToeWorldObb) == false) {
            return false;
        }

        std::array<SimpleMath::Vector3, 4> TopCorners{};
        if (TryResolveObbVerticalCornerPairs(MergedWorldObb, OutCornerPoints, TopCorners) == false) {
            return false;
        }

        for (std::size_t CornerIndex{}; CornerIndex < OutCornerPoints.size(); ++CornerIndex) {
            SimpleMath::Vector3 CornerDirection{ OutCornerPoints[CornerIndex] - TopCorners[CornerIndex] };
            if (TryResolveNormalizedVector(CornerDirection, CornerDirection) == false) {
                return false;
            }

            OutCornerDirections[CornerIndex] = CornerDirection;
        }

        return true;
    }

    bool TryResolveFootTargetOffset(Arche::World& World, const Terrain::ITerrainQuery& TerrainQuery, const Arche::EntityID FootEntityId, const Arche::EntityID ToeEntityId, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, float& OutTargetOffsetY, SimpleMath::Vector3& OutRayOppositeDirection, SimpleMath::Vector3& OutGroundNormal, SimpleMath::Vector3& OutTargetFootPosition, std::size_t& OutHitCount) {
        OutHitCount = 0;
        if (FootEntityId == Arche::NullEntityID || ToeEntityId == Arche::NullEntityID) {
            return false;
        }

        SimpleMath::Matrix FootWorldMatrix{};
        if (TryResolveWorldMatrix(World, FootEntityId, InOutWorldMatrices, FootWorldMatrix) == false) {
            return false;
        }

        const SimpleMath::Vector3 FootWorldPosition{ FootWorldMatrix._41, FootWorldMatrix._42, FootWorldMatrix._43 };
        if (MathUtility::IsFiniteVector3(FootWorldPosition) == false) {
            return false;
        }

        DirectX::BoundingOrientedBox FootWorldObb{};
        if (TryResolveWorldObb(World, FootEntityId, InOutWorldMatrices, FootWorldObb) == false) {
            return false;
        }

        const float FootSoleThickness{ std::abs(FootWorldObb.Extents.y) };
        if (MathUtility::IsFiniteFloat(FootSoleThickness) == false) {
            return false;
        }

        std::array<SimpleMath::Vector3, 4> CornerPoints{};
        std::array<SimpleMath::Vector3, 4> CornerDirections{};
        if (TryResolveFootObbAndToeObbCorners(World, FootEntityId, ToeEntityId, InOutWorldMatrices, CornerPoints, CornerDirections) == false) {
            return false;
        }

        bool IsAnyHit{};
        std::size_t HitCount{};
        SimpleMath::Vector3 HitPointAccumulation{};
        SimpleMath::Vector3 HitNormalAccumulation{};
        SimpleMath::Vector3 RayOppositeDirectionAccumulation{};
        for (std::size_t CornerIndex{}; CornerIndex < CornerPoints.size(); ++CornerIndex) {
            const SimpleMath::Vector3& CornerPoint{ CornerPoints[CornerIndex] };
            const SimpleMath::Vector3& CornerDirection{ CornerDirections[CornerIndex] };
            const SimpleMath::Vector3 RayStartPoint{ CornerPoint - (CornerDirection * FootRaycastStartOffset) };
            if (MathUtility::IsFiniteVector3(RayStartPoint) == false || MathUtility::IsFiniteVector3(CornerDirection) == false) {
                continue;
            }

            const SimpleMath::Ray CornerRay{ RayStartPoint, CornerDirection };
            SimpleMath::Vector3 HitPoint{};
            SimpleMath::Vector3 HitNormal{ SimpleMath::Vector3::Up };
            float HitDistance{};
            const bool IsCornerHit{ TryResolveNearestTerrainRaycastHit(TerrainQuery, CornerRay, FootRaycastLength, HitPoint, HitNormal, HitDistance) };
            if (IsCornerHit == false || MathUtility::IsFiniteVector3(HitPoint) == false || MathUtility::IsFiniteVector3(HitNormal) == false || MathUtility::IsFiniteFloat(HitDistance) == false) {
                continue;
            }

            SimpleMath::Vector3 RayOppositeDirection{ CornerDirection * -1.0f };
            if (TryResolveNormalizedVector(RayOppositeDirection, RayOppositeDirection) == false) {
                continue;
            }

            if (HitNormal.Dot(RayOppositeDirection) < 0.0f) {
                HitNormal *= -1.0f;
            }

            HitPointAccumulation += HitPoint;
            HitNormalAccumulation += HitNormal;
            RayOppositeDirectionAccumulation += RayOppositeDirection;
            ++HitCount;
            IsAnyHit = true;
        }

        if (IsAnyHit == false || HitCount == 0) {
            return false;
        }

        const float HitCountReciprocal{ 1.0f / static_cast<float>(HitCount) };
        SimpleMath::Vector3 AverageRayOppositeDirection{ RayOppositeDirectionAccumulation * HitCountReciprocal };
        SimpleMath::Vector3 AverageHitPoint{ HitPointAccumulation * HitCountReciprocal };
        SimpleMath::Vector3 AverageHitNormal{ HitNormalAccumulation * HitCountReciprocal };
        if (TryResolveNormalizedVector(AverageRayOppositeDirection, AverageRayOppositeDirection) == false || MathUtility::IsFiniteVector3(AverageHitPoint) == false || TryResolveNormalizedVector(AverageHitNormal, AverageHitNormal) == false) {
            return false;
        }

        if (AverageHitNormal.Dot(AverageRayOppositeDirection) < 0.0f) {
            AverageHitNormal *= -1.0f;
        }

        const float FootToPlaneDistance{ (FootWorldPosition - AverageHitPoint).Dot(AverageHitNormal) };
        if (MathUtility::IsFiniteFloat(FootToPlaneDistance) == false) {
            return false;
        }

        const SimpleMath::Vector3 ProjectedFootOnGroundPlane{ FootWorldPosition - (AverageHitNormal * FootToPlaneDistance) };
        const SimpleMath::Vector3 RawTargetFootPosition{ ProjectedFootOnGroundPlane + (AverageHitNormal * FootSoleThickness) };
        if (MathUtility::IsFiniteVector3(RawTargetFootPosition) == false) {
            return false;
        }

        SimpleMath::Vector3 TargetFootPosition{ FootWorldPosition.x, RawTargetFootPosition.y, FootWorldPosition.z };
        if (MathUtility::IsFiniteVector3(TargetFootPosition) == false) {
            return false;
        }

        const float TargetOffsetY{ TargetFootPosition.y - FootWorldPosition.y };
        if (MathUtility::IsFiniteFloat(TargetOffsetY) == false) {
            return false;
        }

        OutTargetOffsetY = TargetOffsetY;
        OutRayOppositeDirection = AverageRayOppositeDirection;
        OutGroundNormal = AverageHitNormal;
        OutTargetFootPosition = TargetFootPosition;
        OutHitCount = HitCount;
        return true;
    }

    bool TryApplyWorldTransformToBoneTransform(Arche::World& World, const Arche::EntityID BoneEntityId, const SimpleMath::Vector3& DesiredWorldPosition, const SimpleMath::Quaternion& DesiredWorldRotation, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        if (BoneEntityId == Arche::NullEntityID || MathUtility::IsFiniteVector3(DesiredWorldPosition) == false || MathUtility::IsFiniteQuaternion(DesiredWorldRotation) == false) {
            return false;
        }

        SimpleMath::Quaternion SafeDesiredWorldRotation{};
        if (TryResolveNormalizedQuaternion(DesiredWorldRotation, SafeDesiredWorldRotation) == false) {
            return false;
        }

        Game::Transform* BoneTransformComponent{ const_cast<Game::Transform*>(std::as_const(World).GetComponent<Game::Transform>(BoneEntityId)) };
        const Game::EntityHierarchy* BoneHierarchyComponent{ std::as_const(World).GetComponent<Game::EntityHierarchy>(BoneEntityId) };
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
        if (IsCurrentWorldDecomposeSucceeded == false || MathUtility::IsFiniteVector3(CurrentWorldScale) == false) {
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
        if (MathUtility::IsFiniteFloat(ParentWorldInverseMatrix._11) == false) {
            return false;
        }

        const SimpleMath::Matrix DesiredBoneLocalWorldMatrix{ DesiredBoneWorldMatrix * ParentWorldInverseMatrix };
        SimpleMath::Matrix NodeToParentInverseMatrix{ BoneTransformComponent->nodeToParent };
        NodeToParentInverseMatrix = NodeToParentInverseMatrix.Invert();
        if (MathUtility::IsFiniteFloat(NodeToParentInverseMatrix._11) == false) {
            return false;
        }

        SimpleMath::Matrix DesiredTrsMatrix{ NodeToParentInverseMatrix * DesiredBoneLocalWorldMatrix };
        SimpleMath::Vector3 DecomposedScale{};
        SimpleMath::Quaternion DecomposedRotation{};
        SimpleMath::Vector3 DecomposedPosition{};
        const bool IsDesiredTrsDecomposeSucceeded{ DesiredTrsMatrix.Decompose(DecomposedScale, DecomposedRotation, DecomposedPosition) };
        if (IsDesiredTrsDecomposeSucceeded == false || MathUtility::IsFiniteVector3(DecomposedPosition) == false || MathUtility::IsFiniteQuaternion(DecomposedRotation) == false) {
            return false;
        }

        DecomposedRotation.Normalize();
        BoneTransformComponent->position = DecomposedPosition;
        BoneTransformComponent->rotation = DecomposedRotation;
        BoneTransformComponent->UpdateEulerRadiansFromRotation();
        InOutWorldMatrices[BoneEntityId] = DesiredBoneWorldMatrix;
        return true;
    }

    bool TryApplyOffsetToBoneTransform(Arche::World& World, const Arche::EntityID BoneEntityId, const float OffsetY, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        if (BoneEntityId == Arche::NullEntityID || MathUtility::IsFiniteFloat(OffsetY) == false) {
            return false;
        }

        const float SafeOffsetY{ std::abs(OffsetY) <= FootOffsetEpsilon ? 0.0f : OffsetY };
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
        if (IsCurrentWorldDecomposeSucceeded == false || MathUtility::IsFiniteVector3(CurrentWorldScale) == false || MathUtility::IsFiniteVector3(CurrentWorldPosition) == false || MathUtility::IsFiniteQuaternion(CurrentWorldRotation) == false) {
            return false;
        }

        if (TryResolveNormalizedQuaternion(CurrentWorldRotation, CurrentWorldRotation) == false) {
            return false;
        }

        SimpleMath::Vector3 DesiredWorldPosition{ CurrentWorldPosition };
        DesiredWorldPosition.y += SafeOffsetY;
        return TryApplyWorldTransformToBoneTransform(World, BoneEntityId, DesiredWorldPosition, CurrentWorldRotation, InOutWorldMatrices);
    }

    bool TryApplyWorldRotationToBoneTransform(Arche::World& World, const Arche::EntityID BoneEntityId, const SimpleMath::Quaternion& DesiredWorldRotation, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        if (BoneEntityId == Arche::NullEntityID || MathUtility::IsFiniteQuaternion(DesiredWorldRotation) == false) {
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
        if (IsCurrentWorldDecomposeSucceeded == false || MathUtility::IsFiniteVector3(CurrentWorldScale) == false || MathUtility::IsFiniteVector3(CurrentWorldPosition) == false) {
            return false;
        }

        return TryApplyWorldTransformToBoneTransform(World, BoneEntityId, CurrentWorldPosition, DesiredWorldRotation, InOutWorldMatrices);
    }

    struct LegWorldState final {
        std::array<SimpleMath::Vector3, 3> WorldPositions{};
        std::array<SimpleMath::Quaternion, 3> WorldRotations{};
        std::array<float, 2> SegmentLengths{};
    };

    bool TryResolveLegWorldState(Arche::World& World, const Arche::EntityID ThighEntityId, const Arche::EntityID ShinEntityId, const Arche::EntityID FootEntityId, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, LegWorldState& OutLegWorldState) {
        if (ThighEntityId == Arche::NullEntityID || ShinEntityId == Arche::NullEntityID || FootEntityId == Arche::NullEntityID) {
            return false;
        }

        const std::array<Arche::EntityID, 3> ChainEntityIds{ { ThighEntityId, ShinEntityId, FootEntityId } };
        std::array<SimpleMath::Matrix, 3> CurrentWorldMatrices{};
        for (std::size_t JointIndex{}; JointIndex < ChainEntityIds.size(); ++JointIndex) {
            if (TryResolveWorldMatrix(World, ChainEntityIds[JointIndex], InOutWorldMatrices, CurrentWorldMatrices[JointIndex]) == false) {
                return false;
            }
        }

        for (std::size_t JointIndex{}; JointIndex < CurrentWorldMatrices.size(); ++JointIndex) {
            SimpleMath::Vector3 CurrentScale{};
            SimpleMath::Quaternion CurrentRotation{};
            SimpleMath::Vector3 CurrentPosition{};
            const bool IsCurrentDecomposeSucceeded{ CurrentWorldMatrices[JointIndex].Decompose(CurrentScale, CurrentRotation, CurrentPosition) };
            if (IsCurrentDecomposeSucceeded == false || MathUtility::IsFiniteVector3(CurrentScale) == false || MathUtility::IsFiniteVector3(CurrentPosition) == false || MathUtility::IsFiniteQuaternion(CurrentRotation) == false || TryResolveNormalizedQuaternion(CurrentRotation, CurrentRotation) == false) {
                return false;
            }

            OutLegWorldState.WorldPositions[JointIndex] = CurrentPosition;
            OutLegWorldState.WorldRotations[JointIndex] = CurrentRotation;
        }

        for (std::size_t SegmentIndex{}; SegmentIndex < OutLegWorldState.SegmentLengths.size(); ++SegmentIndex) {
            const float SegmentLength{ (OutLegWorldState.WorldPositions[SegmentIndex + 1] - OutLegWorldState.WorldPositions[SegmentIndex]).Length() };
            if (MathUtility::IsFiniteFloat(SegmentLength) == false || SegmentLength <= FootOffsetEpsilon) {
                return false;
            }

            OutLegWorldState.SegmentLengths[SegmentIndex] = SegmentLength;
        }

        return true;
    }

    bool TryResolveKneePreferredBendDirection(const std::array<SimpleMath::Vector3, 3>& CurrentWorldPositions, SimpleMath::Vector3& OutPreferredBendDirection) {
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

    bool TryResolveLegReachOverflowDistance(Arche::World& World, const Arche::EntityID ThighEntityId, const Arche::EntityID ShinEntityId, const Arche::EntityID FootEntityId, const SimpleMath::Vector3& TargetFootWorldPosition, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, float& OutReachOverflowDistance) {
        if (MathUtility::IsFiniteVector3(TargetFootWorldPosition) == false) {
            return false;
        }

        LegWorldState CurrentLegWorldState{};
        if (TryResolveLegWorldState(World, ThighEntityId, ShinEntityId, FootEntityId, InOutWorldMatrices, CurrentLegWorldState) == false) {
            return false;
        }

        const float ChainReachDistance{ CurrentLegWorldState.SegmentLengths[0] + CurrentLegWorldState.SegmentLengths[1] };
        const float RootToTargetDistance{ (TargetFootWorldPosition - CurrentLegWorldState.WorldPositions[0]).Length() };
        if (MathUtility::IsFiniteFloat(ChainReachDistance) == false || MathUtility::IsFiniteFloat(RootToTargetDistance) == false) {
            return false;
        }

        OutReachOverflowDistance = std::max(0.0f, RootToTargetDistance - ChainReachDistance);
        return MathUtility::IsFiniteFloat(OutReachOverflowDistance);
    }

    bool TrySolveLegWithIK(Arche::World& World, const Arche::EntityID ThighEntityId, const Arche::EntityID ShinEntityId, const Arche::EntityID FootEntityId, const SimpleMath::Vector3& TargetFootWorldPosition, const Game::IFootIKSolver& FootIKSolver, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        if (ThighEntityId == Arche::NullEntityID || ShinEntityId == Arche::NullEntityID || FootEntityId == Arche::NullEntityID || MathUtility::IsFiniteVector3(TargetFootWorldPosition) == false) {
            return false;
        }

        LegWorldState CurrentLegWorldState{};
        if (TryResolveLegWorldState(World, ThighEntityId, ShinEntityId, FootEntityId, InOutWorldMatrices, CurrentLegWorldState) == false) {
            return false;
        }

        const float CurrentFootToTargetDistance{ (TargetFootWorldPosition - CurrentLegWorldState.WorldPositions.back()).Length() };
        if (MathUtility::IsFiniteFloat(CurrentFootToTargetDistance) == false || CurrentFootToTargetDistance <= FootOffsetEpsilon) {
            return false;
        }

        const std::array<Arche::EntityID, 3> ChainEntityIds{ { ThighEntityId, ShinEntityId, FootEntityId } };
        const std::array<SimpleMath::Vector3, 3>& CurrentWorldPositions{ CurrentLegWorldState.WorldPositions };
        const std::array<SimpleMath::Quaternion, 3>& CurrentWorldRotations{ CurrentLegWorldState.WorldRotations };
        const SimpleMath::Vector3 TargetFootPosition{ TargetFootWorldPosition };

        SimpleMath::Vector3 KneePreferredBendDirection{};
        bool IsKneePreferredBendDirectionResolved{ TryResolveKneePreferredBendDirection(CurrentWorldPositions, KneePreferredBendDirection) };
        if (IsKneePreferredBendDirectionResolved == false) {
            const SimpleMath::Vector3 FallbackKneePreferredBendDirection{ SimpleMath::Vector3::Transform(SimpleMath::Vector3::Right, CurrentWorldRotations[0]) };
            IsKneePreferredBendDirectionResolved = TryResolveNormalizedVector(FallbackKneePreferredBendDirection, KneePreferredBendDirection);
        }

        std::array<Game::FootIKJointConstraint, 1> JointConstraints{};
        JointConstraints[0].mJointIndex = KneeJointIndex;
        JointConstraints[0].mMinimumAngleRadians = KneeMinimumAngleRadians;
        JointConstraints[0].mMaximumAngleRadians = KneeMaximumAngleRadians;
        JointConstraints[0].mUseAngleLimit = true;
        if (IsKneePreferredBendDirectionResolved == true) {
            JointConstraints[0].mPreferredBendDirection = KneePreferredBendDirection;
            JointConstraints[0].mUsePreferredBendDirection = true;
        }

        Game::FootIKSolveParameters SolveParameters{};
        SolveParameters.mJointPositions = std::span<const SimpleMath::Vector3>{ CurrentWorldPositions };
        SolveParameters.mJointConstraints = std::span<const Game::FootIKJointConstraint>{ JointConstraints };
        SolveParameters.mTargetPosition = TargetFootPosition;
        SolveParameters.mMaxIterationCount = FootIKFabrikMaxIterationCount;
        SolveParameters.mConvergenceDistance = FootIKFabrikConvergenceDistance;

        Game::FootIKSolveResult SolveResult{};
        if (FootIKSolver.Solve(SolveParameters, SolveResult) == false || SolveResult.mJointPositions.size() != CurrentWorldPositions.size()) {
            return false;
        }

        bool IsAnyBoneUpdated{};
        for (std::size_t JointIndex{}; JointIndex < ChainEntityIds.size(); ++JointIndex) {
            const SimpleMath::Vector3 DesiredWorldPosition{ SolveResult.mJointPositions[JointIndex] };
            if (MathUtility::IsFiniteVector3(DesiredWorldPosition) == false) {
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

    bool TryResolveToeContactCorrectionDeltaRotation(Arche::World& World, const Terrain::ITerrainQuery& TerrainQuery, const Arche::EntityID ToeEntityId, const SimpleMath::Vector3& FootWorldPosition, const SimpleMath::Quaternion& SurfaceAlignDeltaRotation, const SimpleMath::Vector3& SurfaceNormal, const float AlignmentWeight, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, SimpleMath::Quaternion& OutToeContactCorrectionDeltaRotation) {
        OutToeContactCorrectionDeltaRotation = SimpleMath::Quaternion::Identity;
        if (ToeEntityId == Arche::NullEntityID || MathUtility::IsFiniteVector3(FootWorldPosition) == false || MathUtility::IsFiniteVector3(SurfaceNormal) == false || MathUtility::IsFiniteFloat(AlignmentWeight) == false) {
            return false;
        }

        const float SafeAlignmentWeight{ std::clamp(AlignmentWeight, 0.0f, 1.0f) };
        if (SafeAlignmentWeight <= FootOffsetEpsilon) {
            return false;
        }

        SimpleMath::Vector3 SafeSurfaceNormal{};
        if (TryResolveNormalizedVector(SurfaceNormal, SafeSurfaceNormal) == false) {
            return false;
        }

        DirectX::BoundingOrientedBox ToeWorldObb{};
        if (TryResolveWorldObb(World, ToeEntityId, InOutWorldMatrices, ToeWorldObb) == false) {
            return false;
        }

        const SimpleMath::Vector3 ToeWorldCenter{ ToeWorldObb.Center.x, ToeWorldObb.Center.y, ToeWorldObb.Center.z };
        if (MathUtility::IsFiniteVector3(ToeWorldCenter) == false) {
            return false;
        }

        SimpleMath::Vector3 RotatedToeWorldCenter{};
        if (TryResolveRotatedPointAroundPivot(ToeWorldCenter, FootWorldPosition, SurfaceAlignDeltaRotation, RotatedToeWorldCenter) == false) {
            return false;
        }

        SimpleMath::Vector3 ToeForwardAxis{ RotatedToeWorldCenter - FootWorldPosition };
        const float ToeForwardAxisProjectionOnSurfaceNormal{ ToeForwardAxis.Dot(SafeSurfaceNormal) };
        if (MathUtility::IsFiniteFloat(ToeForwardAxisProjectionOnSurfaceNormal) == false) {
            return false;
        }

        ToeForwardAxis -= SafeSurfaceNormal * ToeForwardAxisProjectionOnSurfaceNormal;
        if (TryResolveNormalizedVector(ToeForwardAxis, ToeForwardAxis) == false) {
            return false;
        }

        SimpleMath::Vector3 ToeRightAxis{ ResolveCrossProduct(SafeSurfaceNormal, ToeForwardAxis) };
        if (TryResolveNormalizedVector(ToeRightAxis, ToeRightAxis) == false) {
            return false;
        }

        std::array<SimpleMath::Vector3, 4> ToeBottomCorners{};
        std::array<SimpleMath::Vector3, 4> ToeTopCorners{};
        if (TryResolveObbVerticalCornerPairs(ToeWorldObb, ToeBottomCorners, ToeTopCorners) == false) {
            return false;
        }

        float ToeContactCorrectionNumerator{};
        float ToeContactCorrectionDenominator{};
        std::size_t ToeHitCount{};
        const SimpleMath::Vector3 ToeCornerRayDirection{ SafeSurfaceNormal * -1.0f };
        for (std::size_t CornerIndex{}; CornerIndex < ToeBottomCorners.size(); ++CornerIndex) {
            const SimpleMath::Vector3& ToeBottomCorner{ ToeBottomCorners[CornerIndex] };
            SimpleMath::Vector3 RotatedToeBottomCorner{};
            if (TryResolveRotatedPointAroundPivot(ToeBottomCorner, FootWorldPosition, SurfaceAlignDeltaRotation, RotatedToeBottomCorner) == false) {
                continue;
            }

            const SimpleMath::Vector3 ToeCornerRayStartPoint{ RotatedToeBottomCorner + (SafeSurfaceNormal * FootRaycastStartOffset) };
            if (MathUtility::IsFiniteVector3(ToeCornerRayStartPoint) == false) {
                continue;
            }

            const SimpleMath::Ray ToeCornerRay{ ToeCornerRayStartPoint, ToeCornerRayDirection };
            SimpleMath::Vector3 ToeCornerHitPoint{};
            SimpleMath::Vector3 ToeCornerHitNormal{};
            float ToeCornerHitDistance{};
            const bool IsToeCornerHit{ TryResolveNearestTerrainRaycastHit(TerrainQuery, ToeCornerRay, FootRaycastLength, ToeCornerHitPoint, ToeCornerHitNormal, ToeCornerHitDistance) };
            if (IsToeCornerHit == false || MathUtility::IsFiniteVector3(ToeCornerHitPoint) == false || MathUtility::IsFiniteVector3(ToeCornerHitNormal) == false || MathUtility::IsFiniteFloat(ToeCornerHitDistance) == false) {
                continue;
            }

            const float ToeCornerDistanceToSurface{ (RotatedToeBottomCorner - ToeCornerHitPoint).Dot(SafeSurfaceNormal) };
            const float ToeCornerLeverArm{ (RotatedToeBottomCorner - FootWorldPosition).Dot(ToeForwardAxis) };
            if (MathUtility::IsFiniteFloat(ToeCornerDistanceToSurface) == false || MathUtility::IsFiniteFloat(ToeCornerLeverArm) == false) {
                continue;
            }

            ToeContactCorrectionNumerator += ToeCornerDistanceToSurface * ToeCornerLeverArm;
            ToeContactCorrectionDenominator += ToeCornerLeverArm * ToeCornerLeverArm;
            ++ToeHitCount;
        }

        if (ToeHitCount < 2 || MathUtility::IsFiniteFloat(ToeContactCorrectionDenominator) == false || ToeContactCorrectionDenominator <= SurfaceNormalLengthEpsilon) {
            return false;
        }

        float ToeContactCorrectionAngle{ ToeContactCorrectionNumerator / ToeContactCorrectionDenominator };
        if (MathUtility::IsFiniteFloat(ToeContactCorrectionAngle) == false) {
            return false;
        }

        ToeContactCorrectionAngle *= SafeAlignmentWeight;
        ToeContactCorrectionAngle = std::clamp(ToeContactCorrectionAngle, -ToeContactCorrectionMaxRadians, ToeContactCorrectionMaxRadians);
        if (std::abs(ToeContactCorrectionAngle) <= FootOffsetEpsilon) {
            return false;
        }

        const SimpleMath::Quaternion ToeContactCorrectionDeltaRotation{ SimpleMath::Quaternion::CreateFromAxisAngle(ToeRightAxis, ToeContactCorrectionAngle) };
        return TryResolveNormalizedQuaternion(ToeContactCorrectionDeltaRotation, OutToeContactCorrectionDeltaRotation);
    }

    bool TryAlignFootToSurface(Arche::World& World, const Terrain::ITerrainQuery& TerrainQuery, const Arche::EntityID FootEntityId, const Arche::EntityID ToeEntityId, const SimpleMath::Vector3& RayOppositeDirection, const SimpleMath::Vector3& SurfaceNormal, const float AlignmentWeight, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        if (FootEntityId == Arche::NullEntityID || MathUtility::IsFiniteVector3(RayOppositeDirection) == false || MathUtility::IsFiniteVector3(SurfaceNormal) == false) {
            return false;
        }

        const float SafeAlignmentWeight{ std::clamp(AlignmentWeight, 0.0f, 1.0f) };
        if (SafeAlignmentWeight <= FootOffsetEpsilon) {
            return false;
        }

        SimpleMath::Vector3 SafeRayOppositeDirection{};
        SimpleMath::Vector3 SafeSurfaceNormal{};
        if (TryResolveNormalizedVector(RayOppositeDirection, SafeRayOppositeDirection) == false || TryResolveNormalizedVector(SurfaceNormal, SafeSurfaceNormal) == false) {
            return false;
        }

        SimpleMath::Vector3 WeightedSurfaceNormal{ SafeRayOppositeDirection + ((SafeSurfaceNormal - SafeRayOppositeDirection) * SafeAlignmentWeight) };
        if (TryResolveNormalizedVector(WeightedSurfaceNormal, WeightedSurfaceNormal) == false) {
            return false;
        }

        SimpleMath::Quaternion SurfaceAlignDeltaRotation{};
        if (TryResolveFromToRotation(SafeRayOppositeDirection, WeightedSurfaceNormal, SurfaceAlignDeltaRotation) == false) {
            return false;
        }

        SimpleMath::Matrix FootWorldMatrix{};
        if (TryResolveWorldMatrix(World, FootEntityId, InOutWorldMatrices, FootWorldMatrix) == false) {
            return false;
        }

        SimpleMath::Vector3 CurrentWorldScale{};
        SimpleMath::Quaternion CurrentWorldRotation{};
        SimpleMath::Vector3 CurrentWorldPosition{};
        const bool IsCurrentFootDecomposeSucceeded{ FootWorldMatrix.Decompose(CurrentWorldScale, CurrentWorldRotation, CurrentWorldPosition) };
        if (IsCurrentFootDecomposeSucceeded == false || MathUtility::IsFiniteVector3(CurrentWorldScale) == false || MathUtility::IsFiniteVector3(CurrentWorldPosition) == false || TryResolveNormalizedQuaternion(CurrentWorldRotation, CurrentWorldRotation) == false) {
            return false;
        }

        SimpleMath::Quaternion DesiredWorldRotation{};
        if (TryResolveWorldRotationWithWorldDelta(CurrentWorldRotation, SurfaceAlignDeltaRotation, DesiredWorldRotation) == false) {
            return false;
        }

        SimpleMath::Quaternion ToeContactCorrectionDeltaRotation{};
        if (TryResolveToeContactCorrectionDeltaRotation(World, TerrainQuery, ToeEntityId, CurrentWorldPosition, SurfaceAlignDeltaRotation, SafeSurfaceNormal, SafeAlignmentWeight, InOutWorldMatrices, ToeContactCorrectionDeltaRotation) == true) {
            if (TryResolveWorldRotationWithWorldDelta(DesiredWorldRotation, ToeContactCorrectionDeltaRotation, DesiredWorldRotation) == false) {
                return false;
            }
        }

        return TryApplyWorldRotationToBoneTransform(World, FootEntityId, DesiredWorldRotation, InOutWorldMatrices);
    }

    float ResolveSharedPelvisOffset(const float LeftOffset, const float RightOffset) {
        const float SafeLeftOffset{ MathUtility::IsFiniteFloat(LeftOffset) ? LeftOffset : 0.0f };
        const float SafeRightOffset{ MathUtility::IsFiniteFloat(RightOffset) ? RightOffset : 0.0f };
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
        const float SafeCurrentOffset{ MathUtility::IsFiniteFloat(CurrentOffset) ? CurrentOffset : 0.0f };
        const float SafeTargetOffset{ MathUtility::IsFiniteFloat(TargetOffset) ? TargetOffset : 0.0f };
        const float SafeBlendSpeed{ std::max(BlendSpeed, 0.0f) };
        const float SafeDt{ std::max(Dt, 0.0f) };
        if (SafeBlendSpeed <= 0.0f || SafeDt <= 0.0f) {
            return SafeTargetOffset;
        }

        const float BlendAlpha{ std::clamp(SafeBlendSpeed * SafeDt, 0.0f, 1.0f) };
        return std::lerp(SafeCurrentOffset, SafeTargetOffset, BlendAlpha);
    }

}
