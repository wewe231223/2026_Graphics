#include "BoundingBoxUpdateSystem.h"

#include <array>
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Transform.h"

namespace Game {
    const std::string& BoundingBoxUpdateSystem::Name() const {
        return mName;
    }

    Phase BoundingBoxUpdateSystem::GetPhase() const {
        return Phase::BoundingBoxUpdate;
    }

    std::span<const ComponentAccess> BoundingBoxUpdateSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 2> Accesses{ { { typeid(Transform), Access::Read }, { typeid(BoundingBox), Access::Write } } };
        return Accesses;
    }

    std::span<const ResourceAccess> BoundingBoxUpdateSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 0> Accesses{};
        return Accesses;
    }

    void BoundingBoxUpdateSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Ctx;
        (void)Dt;

        for (auto [TransformComponent, BoundingBoxComponent] : World.Query<Transform, BoundingBox>()) {
            BoundingBoxComponent.UpdateWorldObb(TransformComponent.worldMatrix);
        }
    }
}
