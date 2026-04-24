#pragma once

/*
PhysicsLib Header Guide
Role:
- Stores shared Transform, RigidBody, BoundingBox, and state flags for all Physics Actors.
Initialization:
- Construct with ActorDesc, or default construct and set mass, bounds, and Transform through setters.
Usage:
- Register as unique_ptr in PhysicsWorld or IPhysicsActorRepository; derived Actors implement collision and integration.
Notes:
- BoundingBox and orientation update through setters, so prefer SetRigidBody or related setters after state changes.
*/

#include <cstdint>
#include <memory>
#include <string>

#include <DirectXCollision.h>
#include <DirectXTK12/SimpleMath.h>

#include "PhysicsLib/Simulation/Types/RigidBody.h"

#ifdef _DEBUG
#pragma comment(lib, "debug/DirectXTK12.lib")
#else
#pragma comment(lib, "release/DirectXTK12.lib")
#endif

class IPhysicsWorldMediator;

class PhysicsActorBase {
public:
    enum class PhysicsActorFlags : std::uint32_t {
        None = 0U,
        Static = 1U << 0U,
        Kinematic = 1U << 1U,
        Trigger = 1U << 2U,
        Sleeping = 1U << 3U
    };

    enum class PhysicsActorType : std::uint32_t {
        Dynamic = 0U,
        Kinematic = 1U,
        Static = 2U
    };

    struct ActorDesc {
        std::string Name{ "PhysicsActor" };
        bool IsActive{ true };
        float Mass{ 1.0F };
        PhysicsActorFlags Flags{ PhysicsActorFlags::None };
        PhysicsActorType ActorType{ PhysicsActorType::Dynamic };
        DirectX::BoundingOrientedBox LocalBoundingBox{};
        DirectX::SimpleMath::Vector3 Position{};
        DirectX::SimpleMath::Vector3 Rotation{};
        DirectX::SimpleMath::Vector3 Scale{ 1.0F, 1.0F, 1.0F };
        DirectX::SimpleMath::Vector3 Velocity{};
        DirectX::SimpleMath::Vector3 Acceleration{};
        float Friction{ 0.6F };
        float Restitution{ 0.1F };
        float LinearDamping{ 0.03F };
        float AngularDamping{ 0.03F };
        bool IsSleeping{};
        float SleepThreshold{ 0.05F };
        float BoundingBoxFatMargin{ 0.1F };
    };

public:
    PhysicsActorBase();
    virtual ~PhysicsActorBase();
    PhysicsActorBase(const PhysicsActorBase& Other);
    PhysicsActorBase& operator=(const PhysicsActorBase& Other);
    PhysicsActorBase(PhysicsActorBase&& Other) noexcept;
    PhysicsActorBase& operator=(PhysicsActorBase&& Other) noexcept;

    explicit PhysicsActorBase(std::string Name);
    explicit PhysicsActorBase(const ActorDesc& Desc);

public:
    void SetName(std::string Name);
    const std::string& GetName() const;

    void SetIsActive(bool IsActive);
    bool GetIsActive() const;

    void SetMass(float Mass);
    float GetMass() const;
    void SetInverseMass(float InverseMass);
    float GetInverseMass() const;
    void SetFriction(float Friction);
    float GetFriction() const;
    void SetLocalInertiaTensor(const DirectX::SimpleMath::Matrix& LocalInertiaTensor);
    const DirectX::SimpleMath::Matrix& GetLocalInertiaTensor() const;
    void SetLocalInverseInertiaTensor(const DirectX::SimpleMath::Matrix& LocalInverseInertiaTensor);
    const DirectX::SimpleMath::Matrix& GetLocalInverseInertiaTensor() const;
    void SetLinearMomentum(const DirectX::SimpleMath::Vector3& LinearMomentum);
    const DirectX::SimpleMath::Vector3& GetLinearMomentum() const;
    void SetAngularMomentum(const DirectX::SimpleMath::Vector3& AngularMomentum);
    const DirectX::SimpleMath::Vector3& GetAngularMomentum() const;
    void SetRigidBody(const RigidBody& RigidBodyState);
    const RigidBody& GetRigidBody() const;

    void SetFlags(PhysicsActorFlags Flags);
    PhysicsActorFlags GetFlags() const;
    bool HasFlag(PhysicsActorFlags Flag) const;

    void SetActorType(PhysicsActorType ActorType);
    PhysicsActorType GetActorType() const;

    void SetLocalBoundingBox(const DirectX::BoundingOrientedBox& LocalBoundingBox);
    const DirectX::BoundingOrientedBox& GetLocalBoundingBox() const;
    const DirectX::BoundingOrientedBox& GetWorldBoundingBox() const;
    const DirectX::BoundingOrientedBox& GetFatWorldBoundingBox() const;

    void SetPosition(const DirectX::SimpleMath::Vector3& Position);
    const DirectX::SimpleMath::Vector3& GetPosition() const;

    void SetRotation(const DirectX::SimpleMath::Vector3& Rotation);
    DirectX::SimpleMath::Vector3 GetRotation() const;
    void SetOrientation(const DirectX::SimpleMath::Quaternion& Orientation);
    const DirectX::SimpleMath::Quaternion& GetOrientation() const;

    void SetScale(const DirectX::SimpleMath::Vector3& Scale);
    const DirectX::SimpleMath::Vector3& GetScale() const;

    void SetVelocity(const DirectX::SimpleMath::Vector3& Velocity);
    const DirectX::SimpleMath::Vector3& GetVelocity() const;

    void SetAcceleration(const DirectX::SimpleMath::Vector3& Acceleration);
    const DirectX::SimpleMath::Vector3& GetAcceleration() const;
    void AddForce(const DirectX::SimpleMath::Vector3& Force);
    const DirectX::SimpleMath::Vector3& GetAccumulatedForce() const;
    void ClearAccumulatedForce();
    void AddImpulse(const DirectX::SimpleMath::Vector3& Impulse);

    void SetRestitution(float Restitution);
    float GetRestitution() const;

    void SetLinearDamping(float LinearDamping);
    float GetLinearDamping() const;

    void SetAngularDamping(float AngularDamping);
    float GetAngularDamping() const;

    void SetSleepThreshold(float SleepThreshold);
    float GetSleepThreshold() const;

    void SetBoundingBoxFatMargin(float BoundingBoxFatMargin);
    float GetBoundingBoxFatMargin() const;

    void SetIsSleeping(bool IsSleeping);
    bool GetIsSleeping() const;

    void MoveToTarget(const DirectX::SimpleMath::Vector3& TargetPosition, float DeltaTime);
    void UpdateSleepState();
    void UpdateWorldBoundingBox();

public:
    virtual bool ResolveDynamicCollision(PhysicsActorBase& DynamicActor, float DeltaTime) const = 0;
    virtual bool ResolveActorCollision(PhysicsActorBase& OtherActor, float DeltaTime) = 0;
    virtual void Integrate(IPhysicsWorldMediator& WorldMediator, float DeltaTime) = 0;
    virtual void SolveConstraints(IPhysicsWorldMediator& WorldMediator, float DeltaTime) = 0;
    virtual std::unique_ptr<PhysicsActorBase> Clone() const = 0;

private:
    void NormalizeRigidBodyOrientation();
    void UpdateFatWorldBoundingBox();

private:
    std::string mName;
    bool mIsActive;
    bool mIsSleeping;
    float mSleepThreshold;
    float mBoundingBoxFatMargin;
    RigidBody mRigidBody;
    PhysicsActorFlags mFlags;
    PhysicsActorType mActorType;
    DirectX::BoundingOrientedBox mLocalBoundingBox;
    DirectX::BoundingOrientedBox mWorldBoundingBox;
    DirectX::BoundingOrientedBox mFatWorldBoundingBox;
};

PhysicsActorBase::PhysicsActorFlags operator|(PhysicsActorBase::PhysicsActorFlags Left, PhysicsActorBase::PhysicsActorFlags Right);
PhysicsActorBase::PhysicsActorFlags operator&(PhysicsActorBase::PhysicsActorFlags Left, PhysicsActorBase::PhysicsActorFlags Right);
