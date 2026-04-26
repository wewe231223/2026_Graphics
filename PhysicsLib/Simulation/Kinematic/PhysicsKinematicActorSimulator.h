#pragma once

#include <vector>

#include <DirectXCollision.h>
#include <DirectXTK12/SimpleMath.h>

#include "PhysicsLib/Actors/PhysicsKinematicActor.h"

class IPhysicsActorRepository;
class IPhysicsWorldMediator;

struct PhysicsKinematicActorSweepBounds final {
    DirectX::SimpleMath::Vector3 mMinimum{};
    DirectX::SimpleMath::Vector3 mMaximum{};
};

struct PhysicsKinematicActorSweepState final {
    PhysicsKinematicActor* mActor{};
    DirectX::SimpleMath::Vector3 mPreviousPosition{};
    DirectX::SimpleMath::Quaternion mPreviousOrientation{ 0.0F, 0.0F, 0.0F, 1.0F };
    DirectX::SimpleMath::Vector3 mPreviousScale{ 1.0F, 1.0F, 1.0F };
    DirectX::SimpleMath::Vector3 mCurrentPosition{};
    DirectX::SimpleMath::Quaternion mCurrentOrientation{ 0.0F, 0.0F, 0.0F, 1.0F };
    DirectX::SimpleMath::Vector3 mCurrentScale{ 1.0F, 1.0F, 1.0F };
    DirectX::SimpleMath::Vector3 mMovementDelta{};
    DirectX::SimpleMath::Vector3 mVelocity{};
    PhysicsKinematicActorSweepBounds mPreviousBounds{};
    PhysicsKinematicActorSweepBounds mCurrentBounds{};
    PhysicsKinematicActorSweepBounds mSweptBounds{};
    float mDeltaTime{};
    bool mIsTeleport{};
    bool mCanUseForCcd{};
};

class PhysicsKinematicActorSimulator final {
public:
    PhysicsKinematicActorSimulator();
    ~PhysicsKinematicActorSimulator();
    PhysicsKinematicActorSimulator(const PhysicsKinematicActorSimulator& Other);
    PhysicsKinematicActorSimulator& operator=(const PhysicsKinematicActorSimulator& Other);
    PhysicsKinematicActorSimulator(PhysicsKinematicActorSimulator&& Other) noexcept;
    PhysicsKinematicActorSimulator& operator=(PhysicsKinematicActorSimulator&& Other) noexcept;

public:
    void Tick(IPhysicsWorldMediator& WorldMediator, IPhysicsActorRepository& ActorRepository, float DeltaTime);
    void Synchronize(IPhysicsActorRepository& ActorRepository);
    void MarkTeleported(const PhysicsKinematicActor& Actor);
    void MarkSweepStatesConsumed();
    const std::vector<PhysicsKinematicActorSweepState>& GetSweepStates() const;

private:
    bool IsMarkedTeleported(const PhysicsKinematicActor& Actor) const;
    void ClearTeleportMark(const PhysicsKinematicActor& Actor);

private:
    std::vector<PhysicsKinematicActorSweepState> mSweepStates;
    std::vector<const PhysicsKinematicActor*> mTeleportedActors;
};
