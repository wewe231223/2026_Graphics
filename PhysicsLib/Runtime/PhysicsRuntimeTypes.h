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
#include <string>
#include <vector>

#include "PhysicsLib/Actors/PhysicsActor.h"
#include "PhysicsLib/Actors/PhysicsDynamicActor.h"
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
    PhysicsDynamicActor::ActorDesc mDynamicActorDesc{};
    PhysicsTerrainActor::ActorDesc mTerrainActorDesc{};
    bool mHasInitialImpulse{};
    DirectX::SimpleMath::Vector3 mInitialImpulse{};
};

struct PhysicsRuntimeScene final {
    std::vector<PhysicsActorSpawnInfo> mActorSpawnInfos{};
};

enum class PhysicsCommandType : std::uint32_t {
    ResetScene = 0U,
    AddImpulse = 1U,
    SetKinematicVelocity = 2U
};

struct PhysicsResetSceneCommand final {
    std::size_t mSceneIndex{};
    std::uint32_t mWorldVersion{};
};

struct PhysicsAddImpulseCommand final {
    ActorId mActorId{ InvalidActorId };
    DirectX::SimpleMath::Vector3 mImpulse{};
};

struct PhysicsSetKinematicVelocityCommand final {
    ActorId mActorId{ InvalidActorId };
    DirectX::SimpleMath::Vector3 mVelocity{};
};

struct PhysicsCommand final {
    PhysicsCommandType mType{ PhysicsCommandType::ResetScene };
    PhysicsResetSceneCommand mResetScene{};
    PhysicsAddImpulseCommand mAddImpulse{};
    PhysicsSetKinematicVelocityCommand mSetKinematicVelocity{};
};

struct PhysicsActorSnapshot final {
    ActorId mActorId{ InvalidActorId };
    PhysicsActorBase::PhysicsActorType mActorType{ PhysicsActorBase::PhysicsActorType::Dynamic };
    bool mIsActive{};
    DirectX::SimpleMath::Vector3 mPosition{};
    DirectX::SimpleMath::Quaternion mOrientation{ 0.0F, 0.0F, 0.0F, 1.0F };
    DirectX::SimpleMath::Vector3 mScale{ 1.0F, 1.0F, 1.0F };
    DirectX::BoundingOrientedBox mWorldBoundingBox{};
};

struct PhysicsSnapshot final {
    std::uint32_t mWorldVersion{};
    std::size_t mSceneIndex{};
    std::size_t mActorCount{};
    std::size_t mLastUpdateStepCount{};
    double mLastUpdateStepElapsedMilliseconds{};
    double mLastStepElapsedMilliseconds{};
    std::vector<PhysicsActorSnapshot> mActors{};
};
