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
}
