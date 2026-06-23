#include "Game/Scene/Components/PhysicsActor.h"

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include "Game/Scene/Components/ComponentInspection.h"

namespace {
    std::string FormatVector3(const DirectX::SimpleMath::Vector3& Value) {
        return std::format("{:.3f}, {:.3f}, {:.3f}", Value.x, Value.y, Value.z);
    }

    std::string_view ResolvePhysicsActorTypeText(PhysicsActorBase::PhysicsActorType ActorType) {
        switch (ActorType) {
            case PhysicsActorBase::PhysicsActorType::Dynamic:
                return "Dynamic";

            case PhysicsActorBase::PhysicsActorType::Kinematic:
                return "Kinematic";

            case PhysicsActorBase::PhysicsActorType::Static:
                return "Static";

            default:
                return "Unknown";
        }
    }
}

namespace Game {
    const char* PhysicsActorSettings::GetComponentInspectionName() {
        return "PhysicsActorSettings";
    }

    void PhysicsActorSettings::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "Name", std::format("{}", GetPhysicsActorSettingsNameTextView(*this)) });
        OutFields.push_back(ComponentInspectionField{ "IsActive", std::format("{}", mIsActive) });
        OutFields.push_back(ComponentInspectionField{ "Mass", std::format("{:.3f}", mMass) });
        OutFields.push_back(ComponentInspectionField{ "Flags", std::format("{}", static_cast<std::uint32_t>(mFlags)) });
        OutFields.push_back(ComponentInspectionField{ "ActorType", std::format("{}", ResolvePhysicsActorTypeText(mActorType)) });
        OutFields.push_back(ComponentInspectionField{ "Friction", std::format("{:.3f}", mFriction) });
        OutFields.push_back(ComponentInspectionField{ "Restitution", std::format("{:.3f}", mRestitution) });
    }

    const char* PhysicsActor::GetComponentInspectionName() {
        return "PhysicsActor";
    }

    void PhysicsActor::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "ActorPointer", std::format("{}", reinterpret_cast<std::uintptr_t>(mActorPointer)) });
        OutFields.push_back(ComponentInspectionField{ "ActorId", std::format("{}", ResolvePhysicsActorId(*this)) });
        OutFields.push_back(ComponentInspectionField{ "ActorIndex", std::format("{}", mActorIndex) });
        OutFields.push_back(ComponentInspectionField{ "ActorType", std::format("{}", ResolvePhysicsActorTypeText(mActorType)) });
        OutFields.push_back(ComponentInspectionField{ "CachedVelocity", FormatVector3(mCachedVelocity) });
    }

    bool PhysicsActor::HasActor() const {
        return mActorId != InvalidPhysicsActorId || mActorPointer != nullptr;
    }

    DirectX::SimpleMath::Vector3 PhysicsActor::GetVelocity() const {
        return mCachedVelocity;
    }

    void PhysicsActor::SetVelocity(const DirectX::SimpleMath::Vector3& Velocity) {
        if (mActorType == PhysicsActorBase::PhysicsActorType::Static) {
            return;
        }

        if (mActorType == PhysicsActorBase::PhysicsActorType::Kinematic) {
            mCachedVelocity = Velocity;

            if (mActorPointer != nullptr) {
                mActorPointer->SetVelocity(Velocity);
                mCachedVelocity = mActorPointer->GetVelocity();
            }

            return;
        }

        mPendingSetVelocity = Velocity;
        mHasPendingSetVelocity = true;
        mCachedVelocity = Velocity;

        if (mActorPointer == nullptr) {
            return;
        }

        mActorPointer->SetVelocity(Velocity);
        mCachedVelocity = mActorPointer->GetVelocity();
    }

    void PhysicsActor::AddForce(const DirectX::SimpleMath::Vector3& Force) {
        if (mActorType == PhysicsActorBase::PhysicsActorType::Kinematic) {
            return;
        }

        if (mActorType != PhysicsActorBase::PhysicsActorType::Dynamic) {
            return;
        }

        mPendingForce += Force;
        mHasPendingForce = true;

        if (mActorPointer == nullptr) {
            return;
        }

        mActorPointer->AddForce(Force);
        mCachedVelocity = mActorPointer->GetVelocity();
    }

    void PhysicsActor::AddImpulse(const DirectX::SimpleMath::Vector3& Impulse) {
        if (mActorType == PhysicsActorBase::PhysicsActorType::Kinematic) {
            return;
        }

        if (mActorType != PhysicsActorBase::PhysicsActorType::Dynamic) {
            return;
        }

        mPendingImpulse += Impulse;
        mHasPendingImpulse = true;

        if (mActorPointer == nullptr) {
            return;
        }

        mActorPointer->AddImpulse(Impulse);
        mCachedVelocity = mActorPointer->GetVelocity();
    }

    std::uint32_t ResolvePhysicsActorId(const PhysicsActor& ActorComponent) {
        if (ActorComponent.mActorId != InvalidPhysicsActorId) {
            return ActorComponent.mActorId;
        }

        if (ActorComponent.mActorPointer != nullptr) {
            return ActorComponent.mActorIndex;
        }

        return InvalidPhysicsActorId;
    }

    void UpdatePhysicsActorCachedSnapshot(PhysicsActor& ActorComponent, const DirectX::SimpleMath::Vector3& Position, const DirectX::SimpleMath::Quaternion& Orientation, const DirectX::SimpleMath::Vector3& Scale, const DirectX::SimpleMath::Vector3& Velocity, const DirectX::BoundingOrientedBox& WorldBoundingBox) {
        ActorComponent.mCachedPosition = Position;
        ActorComponent.mCachedOrientation = Orientation;
        ActorComponent.mCachedScale = Scale;
        ActorComponent.mCachedVelocity = Velocity;
        ActorComponent.mCachedWorldBoundingBox = WorldBoundingBox;
        ActorComponent.mHasCachedSnapshot = true;
    }

    bool TryConsumePhysicsActorPendingCommand(PhysicsActor& ActorComponent, PhysicsCommand& OutCommand) {
        OutCommand = PhysicsCommand{};
        OutCommand.mActorId = static_cast<ActorId>(ResolvePhysicsActorId(ActorComponent));

        if (ActorComponent.mHasPendingSetVelocity == true) {
            OutCommand.mType = PhysicsCommandType::SetVelocity;
            OutCommand.mVector = ActorComponent.mPendingSetVelocity;
            ActorComponent.mPendingSetVelocity = DirectX::SimpleMath::Vector3{};
            ActorComponent.mHasPendingSetVelocity = false;
            return true;
        }

        if (ActorComponent.mHasPendingForce == true) {
            OutCommand.mType = PhysicsCommandType::AddForce;
            OutCommand.mVector = ActorComponent.mPendingForce;
            ActorComponent.mPendingForce = DirectX::SimpleMath::Vector3{};
            ActorComponent.mHasPendingForce = false;
            return true;
        }

        if (ActorComponent.mHasPendingImpulse == true) {
            OutCommand.mType = PhysicsCommandType::AddImpulse;
            OutCommand.mVector = ActorComponent.mPendingImpulse;
            ActorComponent.mPendingImpulse = DirectX::SimpleMath::Vector3{};
            ActorComponent.mHasPendingImpulse = false;
            return true;
        }

        return false;
    }

    PhysicsActorSettings CreatePhysicsActorSettingsComponent(std::string_view SourceName) {
        PhysicsActorSettings NewSettings{};
        NewSettings.mName.Assign(SourceName);
        return NewSettings;
    }

    std::string_view GetPhysicsActorSettingsNameTextView(const PhysicsActorSettings& SettingsComponent) {
        return SettingsComponent.mName.AsStringView();
    }

    const char* GetPhysicsActorSettingsNameText(const PhysicsActorSettings& SettingsComponent) {
        return SettingsComponent.mName.data();
    }

    void SetPhysicsActorSettingsName(PhysicsActorSettings& SettingsComponent, std::string_view SourceName) {
        SettingsComponent.mName.Assign(SourceName);
    }
}
