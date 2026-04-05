#include "SkySystem.h"
#include <array>
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/SkySphere.h"
#include "Game/Scene/Components/Transform.h"

namespace Game {
    const std::string& SkySystem::Name() const {
        return mName;
    }

    Phase SkySystem::GetPhase() const {
        return Phase::PostUpdate;
    }

    std::span<const ComponentAccess> SkySystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 3> Accesses{ { { typeid(Transform), Access::Write }, { typeid(Camera), Access::Read }, { typeid(SkySphere), Access::Read } } };
        return Accesses;
    }

    std::span<const ResourceAccess> SkySystem::ResourceAccesses() const {
        return {};
    }

    void SkySystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Ctx;
        (void)Dt;

        for (const auto [TransformComponent, CameraComponent, SkySphereComponent] : World.Query<Transform, Camera, SkySphere>()) {
            if (CameraComponent.isActive == false) {
                continue;
            }

            if (SkySphereComponent.SkySphereEntityId == Arche::NullEntityID) {
                continue;
            }

            Transform* SkySphereTransformComponent{ World.GetComponent<Transform>(SkySphereComponent.SkySphereEntityId) };
            if (SkySphereTransformComponent == nullptr) {
                continue;
            }

            SkySphereTransformComponent->position = TransformComponent.position;
        }
    }
}
