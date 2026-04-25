#include "Game/Scene/Components/PhysicsActor.h"

#include <cstdint>
#include <format>
#include <string_view>
#include "Game/Scene/Components/ComponentInspection.h"

namespace {
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
        OutFields.push_back(ComponentInspectionField{ "ActorIndex", std::format("{}", mActorIndex) });
        OutFields.push_back(ComponentInspectionField{ "ActorType", std::format("{}", ResolvePhysicsActorTypeText(mActorType)) });
    }

    bool PhysicsActor::HasActor() const {
        return mActorPointer != nullptr;
    }

    void PhysicsActor::AddImpulse(const DirectX::SimpleMath::Vector3& Impulse) {
        if (mActorPointer == nullptr) {
            return;
        }

        mActorPointer->AddImpulse(Impulse);
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
