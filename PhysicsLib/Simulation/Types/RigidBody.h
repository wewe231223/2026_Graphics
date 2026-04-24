#pragma once

/*
PhysicsLib Header Guide
Role:
- Stores linear motion, mass, inertia, and accumulated force state for an Actor.
Initialization:
- Brace initialize with defaults, or configure indirectly through PhysicsActorBase::ActorDesc.
Usage:
- PhysicsActorBase owns it internally and exposes full-state replacement or lookup through SetRigidBody and GetRigidBody.
Notes:
- Direct field changes do not update BoundingBox state, so prefer Actor setters.
*/

#include <DirectXTK12/SimpleMath.h>

struct RigidBody {
    DirectX::SimpleMath::Vector3 mPosition{};
    DirectX::SimpleMath::Quaternion mOrientation{ 0.0F, 0.0F, 0.0F, 1.0F };
    DirectX::SimpleMath::Vector3 mScale{ 1.0F, 1.0F, 1.0F };
    float mMass{ 1.0F };
    float mInverseMass{ 1.0F };
    float mFriction{ 0.6F };
    DirectX::SimpleMath::Matrix mLocalInertiaTensor{ DirectX::SimpleMath::Matrix::Identity };
    DirectX::SimpleMath::Matrix mLocalInverseInertiaTensor{ DirectX::SimpleMath::Matrix::Identity };
    DirectX::SimpleMath::Vector3 mLinearMomentum{};
    DirectX::SimpleMath::Vector3 mAngularMomentum{};
    DirectX::SimpleMath::Vector3 mVelocity{};
    DirectX::SimpleMath::Vector3 mAcceleration{};
    DirectX::SimpleMath::Vector3 mAccumulatedForce{};
    float mRestitution{ 0.1F };
    float mLinearDamping{ 0.03F };
    float mAngularDamping{ 0.03F };
    DirectX::SimpleMath::Vector3 mAngularVelocity{};
    DirectX::SimpleMath::Vector3 mTorque{};
    DirectX::SimpleMath::Matrix mInverseInertiaTensorWorld{ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F };
};
