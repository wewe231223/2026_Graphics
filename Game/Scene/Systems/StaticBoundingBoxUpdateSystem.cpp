#include "StaticBoundingBoxUpdateSystem.h"

#include <array>
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"

namespace Game {
    StaticBoundingBoxUpdateSystem::StaticBoundingBoxUpdateSystem()
        : ParallelSystemBase{} {
    }

    StaticBoundingBoxUpdateSystem::~StaticBoundingBoxUpdateSystem() {
    }

    const std::string& StaticBoundingBoxUpdateSystem::Name() const {
        return mName;
    }

    Phase StaticBoundingBoxUpdateSystem::GetPhase() const {
        return Phase::RenderPrepare;
    }

    std::span<const ComponentAccess> StaticBoundingBoxUpdateSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 4> Accesses{ { { typeid(Transform), Access::Read }, { typeid(StaticMeshRenderer), Access::Read }, { typeid(EntityHierarchy), Access::Read }, { typeid(BoundingBox), Access::Write } } };
        return Accesses;
    }

    std::span<const ResourceAccess> StaticBoundingBoxUpdateSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 0> Accesses{};
        return Accesses;
    }

    void StaticBoundingBoxUpdateSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Ctx;
        (void)Dt;

        for (auto [TransformComponent, Renderer, HierarchyComponent] : World.Query<Transform, StaticMeshRenderer, EntityHierarchy>()) {
            if (Renderer.model == nullptr || Renderer.active == false) {
                continue;
            }

            BoundingBox* BoundingBoxComponent{ World.GetComponent<BoundingBox>(HierarchyComponent.self) };
            if (BoundingBoxComponent == nullptr) {
                continue;
            }

            BoundingBoxComponent->UpdateWorldObb(TransformComponent.worldMatrix);
        }
    }
}
