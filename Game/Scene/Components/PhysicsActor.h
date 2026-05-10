#pragma once

#include <cstdint>
#include <string_view>
#include "Game/Scene/Components/ComponentText.h"
#include "PhysicsLib/Actors/PhysicsActorBase.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
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
            ComponentField(PhysicsActorBase::PhysicsActorType, mActorType, PhysicsActorBase::PhysicsActorType::Dynamic)
        ),
        ComponentMethods(
            ComponentMethod(bool HasActor() const, HasActor)
            ComponentMethod(DirectX::SimpleMath::Vector3 GetVelocity() const, GetVelocity)
            ComponentMethod(void SetVelocity(const DirectX::SimpleMath::Vector3& Velocity), SetVelocity)
            ComponentMethod(void AddForce(const DirectX::SimpleMath::Vector3& Force), AddForce)
            ComponentMethod(void AddImpulse(const DirectX::SimpleMath::Vector3& Impulse), AddImpulse)
        )
    );

    PhysicsActorSettings CreatePhysicsActorSettingsComponent(std::string_view SourceName);
    std::string_view GetPhysicsActorSettingsNameTextView(const PhysicsActorSettings& SettingsComponent);
    const char* GetPhysicsActorSettingsNameText(const PhysicsActorSettings& SettingsComponent);
    void SetPhysicsActorSettingsName(PhysicsActorSettings& SettingsComponent, std::string_view SourceName);
}
