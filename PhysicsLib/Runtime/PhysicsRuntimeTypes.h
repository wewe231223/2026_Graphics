#pragma once

/*
PhysicsLib Header Guide
Role:
- Defines the value types exchanged between the application thread and the PhysicsRuntime thread.
Initialization:
- Build PhysicsRuntimeScene from application scene data before starting PhysicsRuntime.
Usage:
- Use commands for producer-to-runtime requests and snapshots for runtime-to-render state publication.
Notes:
- ActorId values are runtime indices and remain valid only for the scene version that produced them.
*/

#include <DirectXCollision.h>
#include <DirectXTK12/SimpleMath.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "PhysicsLib/Actors/PhysicsActor.h"
#include "PhysicsLib/Actors/PhysicsDynamicActor.h"
#include "PhysicsLib/Actors/PhysicsStaticActor.h"
#include "PhysicsLib/Actors/PhysicsTerrainActor.h"

#undef max
#undef min

using ActorId = std::uint32_t;

constexpr ActorId InvalidActorId{ std::numeric_limits<ActorId>::max() };

struct PhysicsActorSpawnInfo final {
    ActorId mActorId{ InvalidActorId };
    std::string mName{};
    bool mIsActive{ true };
    PhysicsActorBase::PhysicsActorType mActorType{ PhysicsActorBase::PhysicsActorType::Dynamic };
    bool mIsTerrainActor{};
    PhysicsDynamicActor::ActorDesc mDynamicActorDesc{};
    PhysicsTerrainActor::ActorDesc mTerrainActorDesc{};
    bool mHasInitialImpulse{};
    DirectX::SimpleMath::Vector3 mInitialImpulse{};
};

struct PhysicsRuntimeScene final {
    std::vector<PhysicsActorSpawnInfo> mActorSpawnInfos{};
};

struct PhysicsKinematicRuntimeState final {
    ActorId mActorId{ InvalidActorId };
    DirectX::SimpleMath::Vector3 mPosition{};
    DirectX::SimpleMath::Quaternion mOrientation{ 0.0F, 0.0F, 0.0F, 1.0F };
    DirectX::SimpleMath::Vector3 mScale{ 1.0F, 1.0F, 1.0F };
    DirectX::SimpleMath::Vector3 mVelocity{};
    bool mIsActive{ true };
};

enum class PhysicsCommandType : std::uint32_t {
    ResetScene = 0U,
    AddImpulse = 1U,
    SetKinematicVelocity = 2U,
    AddForce = 3U,
    SetVelocity = 4U,
    SetKinematicTransform = 5U,
    SetLocalBoundingBox = 6U,
    SetTerrainActorDesc = 7U,
    SetActorActive = 8U,
    AddStaticActor = 9U
};

struct PhysicsCommand final {
    PhysicsCommandType mType{ PhysicsCommandType::ResetScene };
    ActorId mActorId{ InvalidActorId };
    std::uint32_t mWorldVersion{};
    DirectX::SimpleMath::Vector3 mVector{};
    DirectX::SimpleMath::Vector3 mPosition{};
    DirectX::SimpleMath::Quaternion mOrientation{ 0.0F, 0.0F, 0.0F, 1.0F };
    DirectX::SimpleMath::Vector3 mScale{ 1.0F, 1.0F, 1.0F };
    DirectX::BoundingOrientedBox mLocalBoundingBox{};
    std::shared_ptr<const PhysicsTerrainActor::ActorDesc> mTerrainActorDesc{};
    PhysicsStaticActor::ActorDesc mStaticActorDesc{};
    bool mIsTeleport{};
    bool mSetPosition{ true };
    bool mSetOrientation{ true };
    bool mSetScale{ true };
    bool mIsActive{};
};

struct PhysicsActorSnapshot final {
    ActorId mActorId{ InvalidActorId };
    PhysicsActorBase::PhysicsActorType mActorType{ PhysicsActorBase::PhysicsActorType::Dynamic };
    bool mIsActive{};
    DirectX::SimpleMath::Vector3 mPosition{};
    DirectX::SimpleMath::Quaternion mOrientation{ 0.0F, 0.0F, 0.0F, 1.0F };
    DirectX::SimpleMath::Vector3 mScale{ 1.0F, 1.0F, 1.0F };
    DirectX::SimpleMath::Vector3 mVelocity{};
    DirectX::BoundingOrientedBox mWorldBoundingBox{};
};

struct PhysicsSnapshot final {
    std::uint32_t mWorldVersion{};
    std::uint64_t mStepIndex{};
    double mSimulationTimeSeconds{};
    std::uint64_t mPublishIndex{};
    std::size_t mActorCount{};
    std::size_t mLastUpdateStepCount{};
    double mLastUpdateStepElapsedMilliseconds{};
    double mLastStepElapsedMilliseconds{};
    std::vector<PhysicsActorSnapshot> mActors{};
};
