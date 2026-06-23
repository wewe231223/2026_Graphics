#include "Game/Scene/Systems/CharacterControllerSystem.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "Game/Scene/Components/CharacterController.h"
#include "Game/Scene/Components/PhysicsActor.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    DirectX::SimpleMath::Vector3 MoveTowards(const DirectX::SimpleMath::Vector3& CurrentValue, const DirectX::SimpleMath::Vector3& TargetValue, float MaximumDelta) {
        const DirectX::SimpleMath::Vector3 DeltaValue{ TargetValue - CurrentValue };
        const float DeltaLength{ DeltaValue.Length() };
        if (DeltaLength <= MaximumDelta || DeltaLength <= 0.0F) {
            return TargetValue;
        }

        return CurrentValue + (DeltaValue * (MaximumDelta / DeltaLength));
    }

    float GetActorBottomOffsetFromPositionY(const PhysicsActorBase& Actor) {
        DirectX::XMFLOAT3 Corners[8]{};
        Actor.GetWorldBoundingBox().GetCorners(Corners);

        float MinimumY{ Corners[0].y };
        for (std::size_t CornerIndex{ 1U }; CornerIndex < 8U; ++CornerIndex) {
            MinimumY = std::min(MinimumY, Corners[CornerIndex].y);
        }

        return MinimumY - Actor.GetPosition().y;
    }

    bool TryGetTerrainSurfaceHeight(IPhysicsWorld& PhysicsWorld, float WorldX, float WorldZ, float& OutSurfaceHeight) {
        bool HasSurfaceHeight{};
        const std::vector<PhysicsTerrainActor*> TerrainActors{ PhysicsWorld.CollectTerrainActors() };
        for (PhysicsTerrainActor* TerrainActor : TerrainActors) {
            if (TerrainActor == nullptr) {
                continue;
            }

            float SurfaceHeight{};
            if (TerrainActor->TryGetSurfaceHeightAtWorldPosition(WorldX, WorldZ, SurfaceHeight) == false) {
                continue;
            }

            if (HasSurfaceHeight == false || SurfaceHeight > OutSurfaceHeight) {
                OutSurfaceHeight = SurfaceHeight;
                HasSurfaceHeight = true;
            }
        }

        return HasSurfaceHeight;
    }

    bool TryResolveTerrainGroundContact(IPhysicsWorld& PhysicsWorld, PhysicsActorBase& Actor, float GroundSnapDistance) {
        if (Actor.HasFlag(PhysicsActorBase::PhysicsActorFlags::IgnoreTerrainCollide) == true || Actor.GetVelocity().y > 0.0F) {
            return false;
        }

        float SurfaceHeight{};
        const bool HasSurfaceHeight{ TryGetTerrainSurfaceHeight(PhysicsWorld, Actor.GetPosition().x, Actor.GetPosition().z, SurfaceHeight) };
        if (HasSurfaceHeight == false) {
            return false;
        }

        const float ActorBottomOffsetFromPositionY{ GetActorBottomOffsetFromPositionY(Actor) };
        const float ActorBottomY{ Actor.GetPosition().y + ActorBottomOffsetFromPositionY };
        if (ActorBottomY - SurfaceHeight > GroundSnapDistance) {
            return false;
        }

        DirectX::SimpleMath::Vector3 NextPosition{ Actor.GetPosition() };
        NextPosition.y = SurfaceHeight - ActorBottomOffsetFromPositionY;
        Actor.SetPosition(NextPosition);

        DirectX::SimpleMath::Vector3 NextVelocity{ Actor.GetVelocity() };
        NextVelocity.y = 0.0F;
        Actor.SetVelocity(NextVelocity);
        return true;
    }
}

namespace Game {
    const std::string& CharacterControllerSystem::Name() const {
        return mName;
    }

    Phase CharacterControllerSystem::GetPhase() const {
        return Phase::PreUpdate;
    }

    std::span<const ComponentAccess> CharacterControllerSystem::ComponentAccesses() const {
        static const std::array<ComponentAccess, 3> Accesses{ { { typeid(CharacterController), Access::Write }, { typeid(PhysicsActor), Access::Write }, { typeid(Transform), Access::Write } } };
        return Accesses;
    }

    std::span<const ResourceAccess> CharacterControllerSystem::ResourceAccesses() const {
        static const std::array<ResourceAccess, 1> Accesses{ { { typeid(IPhysicsWorld), Access::Write } } };
        return Accesses;
    }

    void CharacterControllerSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        if (Ctx.PhysicsWorldResource == nullptr || Dt <= 0.0F) {
            return;
        }

        for (auto [CharacterControllerComponent, PhysicsActorComponent, TransformComponent] : World.Query<CharacterController, PhysicsActor, Transform>()) {
            if (CharacterControllerComponent.mIsActive == false || PhysicsActorComponent.mActorType != PhysicsActorBase::PhysicsActorType::Kinematic || PhysicsActorComponent.mActorPointer == nullptr) {
                continue;
            }

            PhysicsActorBase& KinematicActor{ *PhysicsActorComponent.mActorPointer };
            const DirectX::SimpleMath::Vector3 CurrentHorizontalVelocity{ CharacterControllerComponent.mVelocity.x, 0.0F, CharacterControllerComponent.mVelocity.z };
            const float MaximumHorizontalVelocityDelta{ std::max(0.0F, CharacterControllerComponent.mHorizontalAcceleration) * Dt };
            const DirectX::SimpleMath::Vector3 NextHorizontalVelocity{ MoveTowards(CurrentHorizontalVelocity, CharacterControllerComponent.mDesiredHorizontalVelocity, MaximumHorizontalVelocityDelta) };

            DirectX::SimpleMath::Vector3 NextVelocity{ NextHorizontalVelocity.x, CharacterControllerComponent.mVelocity.y, NextHorizontalVelocity.z };
            bool IsJumped{};
            if (CharacterControllerComponent.mHasJumpRequest == true && CharacterControllerComponent.mIsGrounded == true) {
                NextVelocity.y = std::max(0.0F, CharacterControllerComponent.mJumpSpeed);
                IsJumped = true;
            }
            else {
                NextVelocity += Ctx.PhysicsWorldResource->GetGravity() * Dt;
            }

            KinematicActor.SetPosition(TransformComponent.position);
            KinematicActor.SetOrientation(TransformComponent.rotation);
            KinematicActor.SetScale(TransformComponent.scale);
            KinematicActor.SetVelocity(NextVelocity);

            PhysicsCharacterMoveRequest MoveRequest{};
            MoveRequest.mDisplacement = NextVelocity * Dt;
            MoveRequest.mGroundSnapDistance = std::max(0.0F, CharacterControllerComponent.mGroundSnapDistance);
            PhysicsCharacterMoveResult MoveResult{ Ctx.PhysicsWorldResource->MoveKinematicCharacter(KinematicActor, MoveRequest, Dt) };
            if (TryResolveTerrainGroundContact(*Ctx.PhysicsWorldResource, KinematicActor, MoveRequest.mGroundSnapDistance) == true) {
                MoveResult.mPosition = KinematicActor.GetPosition();
                MoveResult.mVelocity = KinematicActor.GetVelocity();
                MoveResult.mIsGrounded = true;
            }

            TransformComponent.position = MoveResult.mPosition;
            CharacterControllerComponent.mVelocity = MoveResult.mVelocity;
            CharacterControllerComponent.mIsGrounded = IsJumped == false && MoveResult.mIsGrounded;
            CharacterControllerComponent.mHasJumpRequest = false;
            PhysicsActorComponent.mCachedVelocity = MoveResult.mVelocity;
        }
    }
}
