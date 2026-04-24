#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <unordered_map>
#include "Arche/World.h"
#include "DirectXTK12/SimpleMath.h"
#include "Game/Scene/Components/FootIKRig.h"
#include "Game/Scene/Components/FootIKRuntime.h"
#include "PhysicsLib/Common.h"

namespace Game {
    class IFootIKSolver;
}

namespace Game::IK {
    bool IsCachedBoneEntityValid(const Arche::World::WorldReadOnlyView& ReadOnlyWorld, Arche::EntityID EntityId, std::string_view ExpectedBoneNameText);
    void ResolveFootBoneEntities(const Arche::World::WorldReadOnlyView& ReadOnlyWorld, const FootIKRig& FootIKRigComponent, Arche::EntityID BoneRootEntityId, FootIKRuntime& InOutFootIKRuntimeComponent);

    bool TryResolveFootObbAndToeObbCorners(Arche::World& World, Arche::EntityID FootEntityId, Arche::EntityID ToeEntityId, std::unordered_map<Arche::EntityID, DirectX::SimpleMath::Matrix>& InOutWorldMatrices, std::array<DirectX::SimpleMath::Vector3, 4>& OutCornerPoints, std::array<DirectX::SimpleMath::Vector3, 4>& OutCornerDirections);
    bool TryResolveFootTargetOffset(Arche::World& World, const IPhysicsWorld& PhysicsWorldInstance, Arche::EntityID FootEntityId, Arche::EntityID ToeEntityId, std::unordered_map<Arche::EntityID, DirectX::SimpleMath::Matrix>& InOutWorldMatrices, float& OutTargetOffsetY, DirectX::SimpleMath::Vector3& OutRayOppositeDirection, DirectX::SimpleMath::Vector3& OutGroundNormal, DirectX::SimpleMath::Vector3& OutTargetFootPosition, std::size_t& OutHitCount);
    bool TryResolveLegReachOverflowDistance(Arche::World& World, Arche::EntityID ThighEntityId, Arche::EntityID ShinEntityId, Arche::EntityID FootEntityId, const DirectX::SimpleMath::Vector3& TargetFootWorldPosition, std::unordered_map<Arche::EntityID, DirectX::SimpleMath::Matrix>& InOutWorldMatrices, float& OutReachOverflowDistance);

    bool TryApplyOffsetToBoneTransform(Arche::World& World, Arche::EntityID BoneEntityId, float OffsetY, std::unordered_map<Arche::EntityID, DirectX::SimpleMath::Matrix>& InOutWorldMatrices);
    bool TrySolveLegWithIK(Arche::World& World, Arche::EntityID ThighEntityId, Arche::EntityID ShinEntityId, Arche::EntityID FootEntityId, const DirectX::SimpleMath::Vector3& TargetFootWorldPosition, const IFootIKSolver& FootIKSolver, std::unordered_map<Arche::EntityID, DirectX::SimpleMath::Matrix>& InOutWorldMatrices);
    bool TryAlignFootToSurface(Arche::World& World, const IPhysicsWorld& PhysicsWorldInstance, Arche::EntityID FootEntityId, Arche::EntityID ToeEntityId, const DirectX::SimpleMath::Vector3& RayOppositeDirection, const DirectX::SimpleMath::Vector3& SurfaceNormal, float AlignmentWeight, std::unordered_map<Arche::EntityID, DirectX::SimpleMath::Matrix>& InOutWorldMatrices);

    float ResolveSharedPelvisOffset(float LeftOffset, float RightOffset);
    float ResolveSmoothedOffset(float CurrentOffset, float TargetOffset, float BlendSpeed, float Dt);
}
