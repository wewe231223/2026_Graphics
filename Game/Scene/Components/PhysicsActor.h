#pragma once

#include <cstdint>
#include <limits>
#include <string_view>
#include "Game/Scene/Components/ComponentText.h"
#include "PhysicsLib/Actors/PhysicsActorBase.h"
#include "PhysicsLib/Runtime/PhysicsRuntimeTypes.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    constexpr std::uint32_t InvalidPhysicsActorId{ std::numeric_limits<std::uint32_t>::max() };

    ComponentDecl(
        PhysicsActorSettings,
        ComponentFields(
            ComponentField(ComponentTextArray, mName)
            ComponentField(bool, mIsActive, true)
            ComponentField(float, mMass, 1.0f)
            ComponentField(PhysicsActorBase::PhysicsActorFlags, mFlags, PhysicsActorBase::PhysicsActorFlags::None)
            ComponentField(PhysicsActorBase::PhysicsActorType, mActorType, PhysicsActorBase::PhysicsActorType::Dynamic)
            ComponentField(float, mFriction, 0.6f)
            ComponentField(float, mRestitution, 0.1f)
        ),
        BOOST_PP_SEQ_NIL
    );

    ComponentDecl(
        PhysicsActor,
        ComponentFields(
            ComponentField(PhysicsActorBase*, mActorPointer, nullptr)
            ComponentField(std::uint32_t, mActorIndex, 0)
            ComponentField(std::uint32_t, mActorId, InvalidPhysicsActorId)
            ComponentField(PhysicsActorBase::PhysicsActorType, mActorType, PhysicsActorBase::PhysicsActorType::Dynamic)
            ComponentField(DirectX::SimpleMath::Vector3, mCachedVelocity, DirectX::SimpleMath::Vector3{})
            ComponentField(DirectX::SimpleMath::Vector3, mCachedPosition, DirectX::SimpleMath::Vector3{})
            ComponentField(DirectX::SimpleMath::Quaternion, mCachedOrientation, DirectX::SimpleMath::Quaternion::Identity)
            ComponentField(DirectX::SimpleMath::Vector3, mCachedScale, DirectX::SimpleMath::Vector3::One)
            ComponentField(DirectX::BoundingOrientedBox, mCachedWorldBoundingBox, DirectX::BoundingOrientedBox{})
            ComponentField(bool, mHasCachedSnapshot, false)
            ComponentField(DirectX::SimpleMath::Vector3, mPendingForce, DirectX::SimpleMath::Vector3{})
            ComponentField(DirectX::SimpleMath::Vector3, mPendingImpulse, DirectX::SimpleMath::Vector3{})
            ComponentField(DirectX::SimpleMath::Vector3, mPendingSetVelocity, DirectX::SimpleMath::Vector3{})
            ComponentField(bool, mHasPendingForce, false)
            ComponentField(bool, mHasPendingImpulse, false)
            ComponentField(bool, mHasPendingSetVelocity, false)
        ),
        ComponentMethods(
            ComponentMethod(bool HasActor() const, HasActor)
            ComponentMethod(DirectX::SimpleMath::Vector3 GetVelocity() const, GetVelocity)
            ComponentMethod(void SetVelocity(const DirectX::SimpleMath::Vector3& Velocity), SetVelocity)
            ComponentMethod(void AddForce(const DirectX::SimpleMath::Vector3& Force), AddForce)
            ComponentMethod(void AddImpulse(const DirectX::SimpleMath::Vector3& Impulse), AddImpulse)
        )
    );

    std::uint32_t ResolvePhysicsActorId(const PhysicsActor& ActorComponent);
    void UpdatePhysicsActorCachedSnapshot(PhysicsActor& ActorComponent, const DirectX::SimpleMath::Vector3& Position, const DirectX::SimpleMath::Quaternion& Orientation, const DirectX::SimpleMath::Vector3& Scale, const DirectX::SimpleMath::Vector3& Velocity, const DirectX::BoundingOrientedBox& WorldBoundingBox);
    bool TryConsumePhysicsActorPendingCommand(PhysicsActor& ActorComponent, PhysicsCommand& OutCommand);

    PhysicsActorSettings CreatePhysicsActorSettingsComponent(std::string_view SourceName);
    std::string_view GetPhysicsActorSettingsNameTextView(const PhysicsActorSettings& SettingsComponent);
    const char* GetPhysicsActorSettingsNameText(const PhysicsActorSettings& SettingsComponent);
    void SetPhysicsActorSettingsName(PhysicsActorSettings& SettingsComponent, std::string_view SourceName);
}
