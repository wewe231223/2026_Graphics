#include "Game/Scene/IK/FootIKAlgorithms.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
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
    constexpr float MinimumFootTiltRadians{ -0.2617993878f };
    constexpr float MaximumFootTiltRadians{ 0.6108652382f };
    constexpr float FootRaycastStartOffset{ 0.3f };
    constexpr float FootRaycastLength{ 0.3f + 0.2f };
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

    float ResolveFootRaycastTargetConfidence(const ::std::size_t HitCount) {
        if (HitCount >= 3) {
            return 1.0f;
        }

        if (HitCount == 2) {
            return 0.5f;
        }

        return 0.0f;
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

    bool TryResolveClampedFromToRotation(const SimpleMath::Vector3& SourceDirection, const SimpleMath::Vector3& TargetDirection, const float MinimumRotationRadians, const float MaximumRotationRadians, const SimpleMath::Vector3& PositiveRotationAxis, SimpleMath::Quaternion& OutRotation) {
        SimpleMath::Vector3 SafeSourceDirection{};
        SimpleMath::Vector3 SafeTargetDirection{};
        SimpleMath::Vector3 SafePositiveRotationAxis{};
        if (TryResolveNormalizedVector(SourceDirection, SafeSourceDirection) == false || TryResolveNormalizedVector(TargetDirection, SafeTargetDirection) == false || TryResolveNormalizedVector(PositiveRotationAxis, SafePositiveRotationAxis) == false || IsFiniteFloat(MinimumRotationRadians) == false || IsFiniteFloat(MaximumRotationRadians) == false) {
            return false;
        }

        float SafeMinimumRotationRadians{ MinimumRotationRadians };
        float SafeMaximumRotationRadians{ MaximumRotationRadians };
        if (SafeMaximumRotationRadians < SafeMinimumRotationRadians) {
            ::std::swap(SafeMinimumRotationRadians, SafeMaximumRotationRadians);
        }

        const float DirectionDot{ ::std::clamp(SafeSourceDirection.Dot(SafeTargetDirection), -1.0f, 1.0f) };
        const float RequiredRotationRadians{ ::std::acos(DirectionDot) };
        if (RequiredRotationRadians <= SurfaceNormalLengthEpsilon) {
            OutRotation = SimpleMath::Quaternion::Identity;
            return true;
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

        const float AxisDirectionSign{ RotationAxis.Dot(SafePositiveRotationAxis) < 0.0f ? -1.0f : 1.0f };
        const float SignedRequiredRotationRadians{ RequiredRotationRadians * AxisDirectionSign };
        const float ClampedSignedRotationRadians{ ::std::clamp(SignedRequiredRotationRadians, SafeMinimumRotationRadians, SafeMaximumRotationRadians) };
        if (::std::abs(ClampedSignedRotationRadians) <= SurfaceNormalLengthEpsilon) {
            OutRotation = SimpleMath::Quaternion::Identity;
            return true;
        }

        const float RotationDirectionSign{ ClampedSignedRotationRadians < 0.0f ? -1.0f : 1.0f };
        const float RotationMagnitudeRadians{ ::std::abs(ClampedSignedRotationRadians) };
        const SimpleMath::Quaternion AxisAngleRotation{ SimpleMath::Quaternion::CreateFromAxisAngle(RotationAxis * RotationDirectionSign, RotationMagnitudeRadians) };
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

    bool TryResolveWorldObb(Arche::World& World, const Arche::EntityID EntityId, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, DirectX::BoundingOrientedBox& OutWorldObb) {
        if (EntityId == Arche::NullEntityID) {
            return false;
        }

        const Game::BoundingBox* BoundingBoxComponent{ ::std::as_const(World).GetComponent<Game::BoundingBox>(EntityId) };
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

    bool TryResolveNearestTerrainRaycastHit(Arche::World& World, const SimpleMath::Ray& Ray, const float RayLength, SimpleMath::Vector3& OutHitPoint, SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) {
        if (IsFiniteVector3(Ray.position) == false || IsFiniteVector3(Ray.direction) == false || IsFiniteFloat(RayLength) == false || RayLength <= 0.0f) {
            return false;
        }

        bool IsHit{};
        float NearestHitDistance{ RayLength };
        SimpleMath::Vector3 NearestHitPoint{};
        SimpleMath::Vector3 NearestHitNormal{ SimpleMath::Vector3::Up };
        for (const auto [TerrainCollideeComponent] : World.Query<Game::TerrainCollidee>()) {
            Game::TerrainHeightResolver* TerrainHeightResolverPointer{ TerrainCollideeComponent.mTerrainHeightResolver };
            if (TerrainHeightResolverPointer == nullptr) {
                continue;
            }

            SimpleMath::Vector3 CandidateHitPoint{};
            SimpleMath::Vector3 CandidateHitNormal{ SimpleMath::Vector3::Up };
            float CandidateHitDistance{};
            const bool IsCandidateHit{ TerrainHeightResolverPointer->TryRaycast(Ray, RayLength, CandidateHitPoint, CandidateHitNormal, CandidateHitDistance) };
            if (IsCandidateHit == false || IsFiniteVector3(CandidateHitPoint) == false || IsFiniteVector3(CandidateHitNormal) == false || IsFiniteFloat(CandidateHitDistance) == false || CandidateHitDistance < 0.0f || CandidateHitDistance > RayLength) {
                continue;
            }

            SimpleMath::Vector3 SafeCandidateHitNormal{};
            if (TryResolveNormalizedVector(CandidateHitNormal, SafeCandidateHitNormal) == false) {
                continue;
            }

            if (IsHit == false || CandidateHitDistance < NearestHitDistance) {
                IsHit = true;
                NearestHitDistance = CandidateHitDistance;
                NearestHitPoint = CandidateHitPoint;
                NearestHitNormal = SafeCandidateHitNormal;
            }
        }

        if (IsHit == false) {
            return false;
        }

        OutHitPoint = NearestHitPoint;
        OutHitNormal = NearestHitNormal;
        OutHitDistance = NearestHitDistance;
        return true;
    }

    bool TryResolveObbCorners(const DirectX::BoundingOrientedBox& WorldObb, ::std::array<SimpleMath::Vector3, 8>& OutCorners) {
        DirectX::XMFLOAT3 RawCorners[8]{};
        WorldObb.GetCorners(RawCorners);
        for (::std::size_t CornerIndex{}; CornerIndex < OutCorners.size(); ++CornerIndex) {
            const SimpleMath::Vector3 CornerPoint{ RawCorners[CornerIndex].x, RawCorners[CornerIndex].y, RawCorners[CornerIndex].z };
            if (IsFiniteVector3(CornerPoint) == false) {
                return false;
            }

            OutCorners[CornerIndex] = CornerPoint;
        }

        return true;
    }

    template <typename Type, typename = void>
    struct HasBoundingOrientedBoxCreateMerged final : ::std::false_type {
    };

    template <typename Type>
    struct HasBoundingOrientedBoxCreateMerged<Type, ::std::void_t<decltype(Type::CreateMerged(::std::declval<Type&>(), ::std::declval<const Type&>(), ::std::declval<const Type&>()))>> final : ::std::true_type {
    };

    template <typename Type>
    bool CreateMerged(Type& OutMergedWorldObb, const Type& LeftWorldObb, const Type& RightWorldObb) {
        if constexpr (HasBoundingOrientedBoxCreateMerged<Type>::value == true) {
            Type::CreateMerged(OutMergedWorldObb, LeftWorldObb, RightWorldObb);
        } else {
            ::std::array<DirectX::XMFLOAT3, 8> LeftCorners{};
            ::std::array<DirectX::XMFLOAT3, 8> RightCorners{};
            LeftWorldObb.GetCorners(LeftCorners.data());
            RightWorldObb.GetCorners(RightCorners.data());

            ::std::array<DirectX::XMFLOAT3, 16> MergedCorners{};
            for (::std::size_t CornerIndex{}; CornerIndex < LeftCorners.size(); ++CornerIndex) {
                MergedCorners[CornerIndex] = LeftCorners[CornerIndex];
                MergedCorners[CornerIndex + LeftCorners.size()] = RightCorners[CornerIndex];
            }

            Type::CreateFromPoints(OutMergedWorldObb, MergedCorners.size(), MergedCorners.data(), sizeof(DirectX::XMFLOAT3));
        }
        const SimpleMath::Vector3 MergedCenter{ OutMergedWorldObb.Center.x, OutMergedWorldObb.Center.y, OutMergedWorldObb.Center.z };
        const SimpleMath::Vector3 MergedExtents{ OutMergedWorldObb.Extents.x, OutMergedWorldObb.Extents.y, OutMergedWorldObb.Extents.z };
        const SimpleMath::Quaternion MergedOrientation{ OutMergedWorldObb.Orientation.x, OutMergedWorldObb.Orientation.y, OutMergedWorldObb.Orientation.z, OutMergedWorldObb.Orientation.w };
        return IsFiniteVector3(MergedCenter) == true && IsFiniteVector3(MergedExtents) == true && IsFiniteQuaternion(MergedOrientation) == true;
    }

    bool TryResolveObbVerticalCornerPairs(const DirectX::BoundingOrientedBox& WorldObb, ::std::array<SimpleMath::Vector3, 4>& OutBottomCorners, ::std::array<SimpleMath::Vector3, 4>& OutTopCorners) {
        const SimpleMath::Vector3 ObbCenter{ WorldObb.Center.x, WorldObb.Center.y, WorldObb.Center.z };
        const SimpleMath::Vector3 ObbExtents{ ::std::abs(WorldObb.Extents.x), ::std::abs(WorldObb.Extents.y), ::std::abs(WorldObb.Extents.z) };
        if (IsFiniteVector3(ObbCenter) == false || IsFiniteVector3(ObbExtents) == false) {
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
        if (IsFiniteFloat(UpAxisWorldUpDot) == false) {
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

        for (::std::size_t CornerIndex{}; CornerIndex < OutBottomCorners.size(); ++CornerIndex) {
            if (IsFiniteVector3(OutBottomCorners[CornerIndex]) == false || IsFiniteVector3(OutTopCorners[CornerIndex]) == false) {
                return false;
            }
        }

        return true;
    }

    bool TryResolveFootAxes(const DirectX::BoundingOrientedBox& FootWorldObb, const DirectX::BoundingOrientedBox& ToeWorldObb, SimpleMath::Vector3& OutForwardAxis, SimpleMath::Vector3& OutRightAxis, SimpleMath::Vector3& OutUpAxis) {
        constexpr SimpleMath::Vector3 WorldUpAxis{ 0.0f, 1.0f, 0.0f };
        constexpr float AxisProjectionEpsilon{ 1.0e-5f };

        const SimpleMath::Vector3 FootCenter{ FootWorldObb.Center.x, FootWorldObb.Center.y, FootWorldObb.Center.z };
        const SimpleMath::Vector3 ToeCenter{ ToeWorldObb.Center.x, ToeWorldObb.Center.y, ToeWorldObb.Center.z };

        SimpleMath::Quaternion FootOrientation{ FootWorldObb.Orientation.x, FootWorldObb.Orientation.y, FootWorldObb.Orientation.z, FootWorldObb.Orientation.w };
        const bool IsFootOrientationResolved{ TryResolveNormalizedQuaternion(FootOrientation, FootOrientation) };
        SimpleMath::Quaternion ToeOrientation{ ToeWorldObb.Orientation.x, ToeWorldObb.Orientation.y, ToeWorldObb.Orientation.z, ToeWorldObb.Orientation.w };
        const bool IsToeOrientationResolved{ TryResolveNormalizedQuaternion(ToeOrientation, ToeOrientation) };

        SimpleMath::Vector3 UpAxis{};
        bool IsUpAxisResolved{};
        if (IsFootOrientationResolved == true) {
            UpAxis = SimpleMath::Vector3::Transform(SimpleMath::Vector3::Up, FootOrientation);
            IsUpAxisResolved = TryResolveNormalizedVector(UpAxis, UpAxis);
        }

        if (IsToeOrientationResolved == true) {
            SimpleMath::Vector3 ToeUpAxis{ SimpleMath::Vector3::Transform(SimpleMath::Vector3::Up, ToeOrientation) };
            if (TryResolveNormalizedVector(ToeUpAxis, ToeUpAxis) == true) {
                if (IsUpAxisResolved == true) {
                    if (UpAxis.Dot(ToeUpAxis) < 0.0f) {
                        ToeUpAxis *= -1.0f;
                    }

                    SimpleMath::Vector3 BlendedUpAxis{ UpAxis + ToeUpAxis };
                    if (TryResolveNormalizedVector(BlendedUpAxis, BlendedUpAxis) == true) {
                        UpAxis = BlendedUpAxis;
                    }
                } else {
                    UpAxis = ToeUpAxis;
                    IsUpAxisResolved = true;
                }
            }
        }

        if (IsUpAxisResolved == false) {
            UpAxis = WorldUpAxis;
            IsUpAxisResolved = true;
        }

        if (IsUpAxisResolved == false) {
            return false;
        }

        const float UpAxisWorldUpDot{ UpAxis.Dot(WorldUpAxis) };
        if (IsFiniteFloat(UpAxisWorldUpDot) == false) {
            return false;
        }

        if (UpAxisWorldUpDot < 0.0f) {
            UpAxis *= -1.0f;
        }

        SimpleMath::Vector3 ForwardAxis{ ToeCenter - FootCenter };
        const float ForwardAxisProjectionOnUp{ ForwardAxis.Dot(UpAxis) };
        if (IsFiniteFloat(ForwardAxisProjectionOnUp) == false) {
            return false;
        }

        ForwardAxis -= UpAxis * ForwardAxisProjectionOnUp;
        if (TryResolveNormalizedVector(ForwardAxis, ForwardAxis) == false) {
            if (IsFootOrientationResolved == true) {
                ForwardAxis = SimpleMath::Vector3::Transform(SimpleMath::Vector3::Forward, FootOrientation);
                const float FallbackForwardProjectionOnUp{ ForwardAxis.Dot(UpAxis) };
                if (IsFiniteFloat(FallbackForwardProjectionOnUp) == false) {
                    return false;
                }

                ForwardAxis -= UpAxis * FallbackForwardProjectionOnUp;
            }
        }

        if (TryResolveNormalizedVector(ForwardAxis, ForwardAxis) == false) {
            if (IsToeOrientationResolved == true) {
                ForwardAxis = SimpleMath::Vector3::Transform(SimpleMath::Vector3::Forward, ToeOrientation);
                const float FallbackForwardProjectionOnUp{ ForwardAxis.Dot(UpAxis) };
                if (IsFiniteFloat(FallbackForwardProjectionOnUp) == false) {
                    return false;
                }

                ForwardAxis -= UpAxis * FallbackForwardProjectionOnUp;
            }
        }

        if (TryResolveNormalizedVector(ForwardAxis, ForwardAxis) == false) {
            ForwardAxis = ResolveCrossProduct(UpAxis, SimpleMath::Vector3::Right);
            if (TryResolveNormalizedVector(ForwardAxis, ForwardAxis) == false) {
                ForwardAxis = ResolveCrossProduct(UpAxis, SimpleMath::Vector3::Forward);
                if (TryResolveNormalizedVector(ForwardAxis, ForwardAxis) == false) {
                    return false;
                }
            }
        }

        SimpleMath::Vector3 RightAxis{ ResolveCrossProduct(UpAxis, ForwardAxis) };
        if (TryResolveNormalizedVector(RightAxis, RightAxis) == false) {
            return false;
        }

        SimpleMath::Vector3 RecomputedForwardAxis{ ResolveCrossProduct(RightAxis, UpAxis) };
        if (TryResolveNormalizedVector(RecomputedForwardAxis, RecomputedForwardAxis) == true) {
            ForwardAxis = RecomputedForwardAxis;
        }

        const float ForwardUpOrthogonality{ ::std::abs(ForwardAxis.Dot(UpAxis)) };
        const float RightUpOrthogonality{ ::std::abs(RightAxis.Dot(UpAxis)) };
        if (IsFiniteFloat(ForwardUpOrthogonality) == false || IsFiniteFloat(RightUpOrthogonality) == false || ForwardUpOrthogonality > AxisProjectionEpsilon || RightUpOrthogonality > AxisProjectionEpsilon) {
            return false;
        }

        OutForwardAxis = ForwardAxis;
        OutRightAxis = RightAxis;
        OutUpAxis = UpAxis;

        return true;
    }

    bool TryResolveMergedObbVerticalCornerPairs(const ::std::array<SimpleMath::Vector3, 8>& FootCorners, const ::std::array<SimpleMath::Vector3, 8>& ToeCorners, const SimpleMath::Vector3& ForwardAxis, const SimpleMath::Vector3& RightAxis, const SimpleMath::Vector3& UpAxis, ::std::array<SimpleMath::Vector3, 4>& OutBottomCorners, ::std::array<SimpleMath::Vector3, 4>& OutTopCorners) {
        float MinimumForwardProjection{ (::std::numeric_limits<float>::max)() };
        float MaximumForwardProjection{ -(::std::numeric_limits<float>::max)() };
        float MinimumRightProjection{ (::std::numeric_limits<float>::max)() };
        float MaximumRightProjection{ -(::std::numeric_limits<float>::max)() };
        float MinimumUpProjection{ (::std::numeric_limits<float>::max)() };
        float MaximumUpProjection{ -(::std::numeric_limits<float>::max)() };

        const auto UpdateProjectionExtents = [&ForwardAxis, &RightAxis, &UpAxis, &MinimumForwardProjection, &MaximumForwardProjection, &MinimumRightProjection, &MaximumRightProjection, &MinimumUpProjection, &MaximumUpProjection](const SimpleMath::Vector3& CornerPoint) -> bool {
            const float ForwardProjection{ CornerPoint.Dot(ForwardAxis) };
            const float RightProjection{ CornerPoint.Dot(RightAxis) };
            const float UpProjection{ CornerPoint.Dot(UpAxis) };
            if (IsFiniteFloat(ForwardProjection) == false || IsFiniteFloat(RightProjection) == false || IsFiniteFloat(UpProjection) == false) {
                return false;
            }

            MinimumForwardProjection = (::std::min)(MinimumForwardProjection, ForwardProjection);
            MaximumForwardProjection = (::std::max)(MaximumForwardProjection, ForwardProjection);
            MinimumRightProjection = (::std::min)(MinimumRightProjection, RightProjection);
            MaximumRightProjection = (::std::max)(MaximumRightProjection, RightProjection);
            MinimumUpProjection = (::std::min)(MinimumUpProjection, UpProjection);
            MaximumUpProjection = (::std::max)(MaximumUpProjection, UpProjection);
            return true;
        };

        for (const SimpleMath::Vector3& FootCorner : FootCorners) {
            if (UpdateProjectionExtents(FootCorner) == false) {
                return false;
            }
        }

        for (const SimpleMath::Vector3& ToeCorner : ToeCorners) {
            if (UpdateProjectionExtents(ToeCorner) == false) {
                return false;
            }
        }

        if (MinimumForwardProjection > MaximumForwardProjection || MinimumRightProjection > MaximumRightProjection || MinimumUpProjection > MaximumUpProjection) {
            return false;
        }

        const float CenterForwardProjection{ (MinimumForwardProjection + MaximumForwardProjection) * 0.5f };
        const float CenterRightProjection{ (MinimumRightProjection + MaximumRightProjection) * 0.5f };
        const float CenterUpProjection{ (MinimumUpProjection + MaximumUpProjection) * 0.5f };
        const float ExtentForward{ (MaximumForwardProjection - MinimumForwardProjection) * 0.5f };
        const float ExtentRight{ (MaximumRightProjection - MinimumRightProjection) * 0.5f };
        const float ExtentUp{ (MaximumUpProjection - MinimumUpProjection) * 0.5f };
        if (IsFiniteFloat(CenterForwardProjection) == false || IsFiniteFloat(CenterRightProjection) == false || IsFiniteFloat(CenterUpProjection) == false || IsFiniteFloat(ExtentForward) == false || IsFiniteFloat(ExtentRight) == false || IsFiniteFloat(ExtentUp) == false) {
            return false;
        }

        const SimpleMath::Vector3 MergedCenter{ (ForwardAxis * CenterForwardProjection) + (RightAxis * CenterRightProjection) + (UpAxis * CenterUpProjection) };
        const SimpleMath::Vector3 BottomFaceCenter{ MergedCenter - (UpAxis * ExtentUp) };
        const SimpleMath::Vector3 TopFaceCenter{ MergedCenter + (UpAxis * ExtentUp) };
        const SimpleMath::Vector3 RightExtentOffset{ RightAxis * ExtentRight };
        const SimpleMath::Vector3 ForwardExtentOffset{ ForwardAxis * ExtentForward };

        OutBottomCorners[0] = BottomFaceCenter - RightExtentOffset - ForwardExtentOffset;
        OutBottomCorners[1] = BottomFaceCenter + RightExtentOffset - ForwardExtentOffset;
        OutBottomCorners[2] = BottomFaceCenter - RightExtentOffset + ForwardExtentOffset;
        OutBottomCorners[3] = BottomFaceCenter + RightExtentOffset + ForwardExtentOffset;
        OutTopCorners[0] = TopFaceCenter - RightExtentOffset - ForwardExtentOffset;
        OutTopCorners[1] = TopFaceCenter + RightExtentOffset - ForwardExtentOffset;
        OutTopCorners[2] = TopFaceCenter - RightExtentOffset + ForwardExtentOffset;
        OutTopCorners[3] = TopFaceCenter + RightExtentOffset + ForwardExtentOffset;
        for (::std::size_t CornerIndex{}; CornerIndex < OutBottomCorners.size(); ++CornerIndex) {
            if (IsFiniteVector3(OutBottomCorners[CornerIndex]) == false || IsFiniteVector3(OutTopCorners[CornerIndex]) == false) {
                return false;
            }
        }

        return true;
    }

    bool TryResolveFootObbAndToeObbCorners(Arche::World& World, const Arche::EntityID FootEntityId, const Arche::EntityID ToeEntityId, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, ::std::array<SimpleMath::Vector3, 4>& OutCornerPoints, ::std::array<SimpleMath::Vector3, 4>& OutCornerDirections) {
        DirectX::BoundingOrientedBox FootWorldObb{};
        DirectX::BoundingOrientedBox ToeWorldObb{};
        if (TryResolveWorldObb(World, FootEntityId, InOutWorldMatrices, FootWorldObb) == false || TryResolveWorldObb(World, ToeEntityId, InOutWorldMatrices, ToeWorldObb) == false) {
            return false;
        }

        DirectX::BoundingOrientedBox MergedWorldObb{};
        if (CreateMerged(MergedWorldObb, FootWorldObb, ToeWorldObb) == false) {
            return false;
        }

        ::std::array<SimpleMath::Vector3, 4> TopCorners{};
        if (TryResolveObbVerticalCornerPairs(MergedWorldObb, OutCornerPoints, TopCorners) == false) {
            return false;
        }

        for (::std::size_t CornerIndex{}; CornerIndex < OutCornerPoints.size(); ++CornerIndex) {
            SimpleMath::Vector3 CornerDirection{ OutCornerPoints[CornerIndex] - TopCorners[CornerIndex] };
            if (TryResolveNormalizedVector(CornerDirection, CornerDirection) == false) {
                return false;
            }

            OutCornerDirections[CornerIndex] = CornerDirection;
        }

        return true;
    }

    bool TryResolveFootTargetOffset(Arche::World& World, const Arche::EntityID FootEntityId, const Arche::EntityID ToeEntityId, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, float& OutTargetOffsetY, SimpleMath::Vector3& OutGroundNormal, SimpleMath::Vector3& OutTargetFootPosition, float& OutTargetConfidence) {
        if (FootEntityId == Arche::NullEntityID || ToeEntityId == Arche::NullEntityID) {
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

        DirectX::BoundingOrientedBox FootWorldObb{};
        if (TryResolveWorldObb(World, FootEntityId, InOutWorldMatrices, FootWorldObb) == false) {
            return false;
        }

        const float FootSoleThickness{ ::std::abs(FootWorldObb.Extents.y) };
        if (IsFiniteFloat(FootSoleThickness) == false) {
            return false;
        }

        ::std::array<SimpleMath::Vector3, 4> CornerPoints{};
        ::std::array<SimpleMath::Vector3, 4> CornerDirections{};
        if (TryResolveFootObbAndToeObbCorners(World, FootEntityId, ToeEntityId, InOutWorldMatrices, CornerPoints, CornerDirections) == false) {
            return false;
        }

        bool IsAnyHit{};
        ::std::size_t HitCount{};
        SimpleMath::Vector3 HitPointAccumulation{};
        SimpleMath::Vector3 HitNormalAccumulation{};
        for (::std::size_t CornerIndex{}; CornerIndex < CornerPoints.size(); ++CornerIndex) {
            const SimpleMath::Vector3& CornerPoint{ CornerPoints[CornerIndex] };
            const SimpleMath::Vector3& CornerDirection{ CornerDirections[CornerIndex] };
            const SimpleMath::Vector3 RayStartPoint{ CornerPoint - (CornerDirection * FootRaycastStartOffset) };
            if (IsFiniteVector3(RayStartPoint) == false || IsFiniteVector3(CornerDirection) == false) {
                continue;
            }

            const SimpleMath::Ray CornerRay{ RayStartPoint, CornerDirection };
            SimpleMath::Vector3 HitPoint{};
            SimpleMath::Vector3 HitNormal{ SimpleMath::Vector3::Up };
            float HitDistance{};
            const bool IsCornerHit{ TryResolveNearestTerrainRaycastHit(World, CornerRay, FootRaycastLength, HitPoint, HitNormal, HitDistance) };
            if (IsCornerHit == false || IsFiniteVector3(HitPoint) == false || IsFiniteVector3(HitNormal) == false || IsFiniteFloat(HitDistance) == false) {
                continue;
            }

            HitPointAccumulation += HitPoint;
            HitNormalAccumulation += HitNormal;
            ++HitCount;
            IsAnyHit = true;
        }

        if (IsAnyHit == false || HitCount == 0) {
            return false;
        }

        const float TargetConfidence{ ResolveFootRaycastTargetConfidence(HitCount) };
        if (IsFiniteFloat(TargetConfidence) == false || TargetConfidence <= 0.0f) {
            return false;
        }

        const float HitCountReciprocal{ 1.0f / static_cast<float>(HitCount) };
        SimpleMath::Vector3 AverageHitPoint{ HitPointAccumulation * HitCountReciprocal };
        SimpleMath::Vector3 AverageHitNormal{ HitNormalAccumulation * HitCountReciprocal };
        if (IsFiniteVector3(AverageHitPoint) == false || TryResolveNormalizedVector(AverageHitNormal, AverageHitNormal) == false) {
            return false;
        }

        const float FootToPlaneDistance{ (FootWorldPosition - AverageHitPoint).Dot(AverageHitNormal) };
        if (IsFiniteFloat(FootToPlaneDistance) == false) {
            return false;
        }

        const SimpleMath::Vector3 ProjectedFootOnGroundPlane{ FootWorldPosition - (AverageHitNormal * FootToPlaneDistance) };
        const SimpleMath::Vector3 TargetFootPosition{ ProjectedFootOnGroundPlane + (AverageHitNormal * FootSoleThickness) };
        if (IsFiniteVector3(TargetFootPosition) == false) {
            return false;
        }

        const float TargetOffsetY{ TargetFootPosition.y - FootWorldPosition.y };
        if (IsFiniteFloat(TargetOffsetY) == false) {
            return false;
        }

        OutTargetOffsetY = TargetOffsetY;
        OutGroundNormal = AverageHitNormal;
        OutTargetFootPosition = TargetFootPosition;
        OutTargetConfidence = TargetConfidence;
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

    struct LegWorldState final {
        ::std::array<SimpleMath::Vector3, 3> WorldPositions{};
        ::std::array<SimpleMath::Quaternion, 3> WorldRotations{};
        ::std::array<float, 2> SegmentLengths{};
    };

    bool TryResolveLegWorldState(Arche::World& World, const Arche::EntityID ThighEntityId, const Arche::EntityID ShinEntityId, const Arche::EntityID FootEntityId, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, LegWorldState& OutLegWorldState) {
        if (ThighEntityId == Arche::NullEntityID || ShinEntityId == Arche::NullEntityID || FootEntityId == Arche::NullEntityID) {
            return false;
        }

        const ::std::array<Arche::EntityID, 3> ChainEntityIds{ { ThighEntityId, ShinEntityId, FootEntityId } };
        ::std::array<SimpleMath::Matrix, 3> CurrentWorldMatrices{};
        for (::std::size_t JointIndex{}; JointIndex < ChainEntityIds.size(); ++JointIndex) {
            if (TryResolveWorldMatrix(World, ChainEntityIds[JointIndex], InOutWorldMatrices, CurrentWorldMatrices[JointIndex]) == false) {
                return false;
            }
        }

        for (::std::size_t JointIndex{}; JointIndex < CurrentWorldMatrices.size(); ++JointIndex) {
            SimpleMath::Vector3 CurrentScale{};
            SimpleMath::Quaternion CurrentRotation{};
            SimpleMath::Vector3 CurrentPosition{};
            const bool IsCurrentDecomposeSucceeded{ CurrentWorldMatrices[JointIndex].Decompose(CurrentScale, CurrentRotation, CurrentPosition) };
            if (IsCurrentDecomposeSucceeded == false || IsFiniteVector3(CurrentScale) == false || IsFiniteVector3(CurrentPosition) == false || IsFiniteQuaternion(CurrentRotation) == false || TryResolveNormalizedQuaternion(CurrentRotation, CurrentRotation) == false) {
                return false;
            }

            OutLegWorldState.WorldPositions[JointIndex] = CurrentPosition;
            OutLegWorldState.WorldRotations[JointIndex] = CurrentRotation;
        }

        for (::std::size_t SegmentIndex{}; SegmentIndex < OutLegWorldState.SegmentLengths.size(); ++SegmentIndex) {
            const float SegmentLength{ (OutLegWorldState.WorldPositions[SegmentIndex + 1] - OutLegWorldState.WorldPositions[SegmentIndex]).Length() };
            if (IsFiniteFloat(SegmentLength) == false || SegmentLength <= FootOffsetEpsilon) {
                return false;
            }

            OutLegWorldState.SegmentLengths[SegmentIndex] = SegmentLength;
        }

        return true;
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

    bool TryResolveLegReachOverflowDistance(Arche::World& World, const Arche::EntityID ThighEntityId, const Arche::EntityID ShinEntityId, const Arche::EntityID FootEntityId, const SimpleMath::Vector3& TargetFootWorldPosition, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, float& OutReachOverflowDistance) {
        if (IsFiniteVector3(TargetFootWorldPosition) == false) {
            return false;
        }

        LegWorldState CurrentLegWorldState{};
        if (TryResolveLegWorldState(World, ThighEntityId, ShinEntityId, FootEntityId, InOutWorldMatrices, CurrentLegWorldState) == false) {
            return false;
        }

        const float ChainReachDistance{ CurrentLegWorldState.SegmentLengths[0] + CurrentLegWorldState.SegmentLengths[1] };
        const float RootToTargetDistance{ (TargetFootWorldPosition - CurrentLegWorldState.WorldPositions[0]).Length() };
        if (IsFiniteFloat(ChainReachDistance) == false || IsFiniteFloat(RootToTargetDistance) == false) {
            return false;
        }

        OutReachOverflowDistance = (::std::max)(0.0f, RootToTargetDistance - ChainReachDistance);
        return IsFiniteFloat(OutReachOverflowDistance);
    }

    bool TrySolveLegWithIK(Arche::World& World, const Arche::EntityID ThighEntityId, const Arche::EntityID ShinEntityId, const Arche::EntityID FootEntityId, const SimpleMath::Vector3& TargetFootWorldPosition, const Game::IFootIKSolver& FootIKSolver, ::std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices) {
        if (ThighEntityId == Arche::NullEntityID || ShinEntityId == Arche::NullEntityID || FootEntityId == Arche::NullEntityID || IsFiniteVector3(TargetFootWorldPosition) == false) {
            return false;
        }

        LegWorldState CurrentLegWorldState{};
        if (TryResolveLegWorldState(World, ThighEntityId, ShinEntityId, FootEntityId, InOutWorldMatrices, CurrentLegWorldState) == false) {
            return false;
        }

        const float CurrentFootToTargetDistance{ (TargetFootWorldPosition - CurrentLegWorldState.WorldPositions.back()).Length() };
        if (IsFiniteFloat(CurrentFootToTargetDistance) == false || CurrentFootToTargetDistance <= FootOffsetEpsilon) {
            return false;
        }

        const ::std::array<Arche::EntityID, 3> ChainEntityIds{ { ThighEntityId, ShinEntityId, FootEntityId } };
        const ::std::array<SimpleMath::Vector3, 3>& CurrentWorldPositions{ CurrentLegWorldState.WorldPositions };
        const ::std::array<SimpleMath::Quaternion, 3>& CurrentWorldRotations{ CurrentLegWorldState.WorldRotations };
        const SimpleMath::Vector3 TargetFootPosition{ TargetFootWorldPosition };

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

        SimpleMath::Vector3 FootRightDirection{ ResolveCrossProduct(FootSurfaceAlignmentDataValue.CurrentFootNormal, FootSurfaceAlignmentDataValue.SafeFootToToeDirection) };
        if (TryResolveNormalizedVector(FootRightDirection, FootRightDirection) == false) {
            return false;
        }

        SimpleMath::Quaternion SurfaceAlignDeltaRotation{};
        if (TryResolveClampedFromToRotation(FootSurfaceAlignmentDataValue.CurrentFootNormal, DesiredSurfaceNormal, MinimumFootTiltRadians, MaximumFootTiltRadians, FootRightDirection, SurfaceAlignDeltaRotation) == false) {
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

    bool TryResolveFootObbAndToeObbCorners(Arche::World& World, const Arche::EntityID FootEntityId, const Arche::EntityID ToeEntityId, ::std::unordered_map<Arche::EntityID, DirectX::SimpleMath::Matrix>& InOutWorldMatrices, ::std::array<DirectX::SimpleMath::Vector3, 4>& OutCornerPoints, ::std::array<DirectX::SimpleMath::Vector3, 4>& OutCornerDirections) {
        return ::TryResolveFootObbAndToeObbCorners(World, FootEntityId, ToeEntityId, InOutWorldMatrices, OutCornerPoints, OutCornerDirections);
    }

    bool TryResolveFootTargetOffset(Arche::World& World, const Arche::EntityID FootEntityId, const Arche::EntityID ToeEntityId, ::std::unordered_map<Arche::EntityID, DirectX::SimpleMath::Matrix>& InOutWorldMatrices, float& OutTargetOffsetY, DirectX::SimpleMath::Vector3& OutGroundNormal, DirectX::SimpleMath::Vector3& OutTargetFootPosition, float& OutTargetConfidence) {
        return ::TryResolveFootTargetOffset(World, FootEntityId, ToeEntityId, InOutWorldMatrices, OutTargetOffsetY, OutGroundNormal, OutTargetFootPosition, OutTargetConfidence);
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

    bool TryResolveLegReachOverflowDistance(Arche::World& World, const Arche::EntityID ThighEntityId, const Arche::EntityID ShinEntityId, const Arche::EntityID FootEntityId, const DirectX::SimpleMath::Vector3& TargetFootWorldPosition, ::std::unordered_map<Arche::EntityID, DirectX::SimpleMath::Matrix>& InOutWorldMatrices, float& OutReachOverflowDistance) {
        return ::TryResolveLegReachOverflowDistance(World, ThighEntityId, ShinEntityId, FootEntityId, TargetFootWorldPosition, InOutWorldMatrices, OutReachOverflowDistance);
    }

    bool TrySolveLegWithIK(Arche::World& World, const Arche::EntityID ThighEntityId, const Arche::EntityID ShinEntityId, const Arche::EntityID FootEntityId, const DirectX::SimpleMath::Vector3& TargetFootWorldPosition, const IFootIKSolver& FootIKSolver, ::std::unordered_map<Arche::EntityID, DirectX::SimpleMath::Matrix>& InOutWorldMatrices) {
        return ::TrySolveLegWithIK(World, ThighEntityId, ShinEntityId, FootEntityId, TargetFootWorldPosition, FootIKSolver, InOutWorldMatrices);
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
