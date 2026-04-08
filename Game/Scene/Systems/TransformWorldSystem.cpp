#include "TransformWorldSystem.h"

#include <array>
#include <vector>

#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    SimpleMath::Matrix BuildLocalWorldMatrix(const Game::Transform& TransformComponent) {
        const SimpleMath::Matrix TrsMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
        return TransformComponent.nodeToParent * TrsMatrix;
    }

    struct PendingEntity final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        SimpleMath::Matrix ParentWorldMatrix{ SimpleMath::Matrix::Identity };
    };
}

namespace Game {
    const std::string& TransformWorldSystem::Name() const {
        return mName;
    }

    Phase TransformWorldSystem::GetPhase() const {
        return Phase::Transform;
    }

    std::span<const ComponentAccess> TransformWorldSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 2> Accesses{ { { typeid(Transform), Access::Write }, { typeid(EntityHierarchy), Access::Read } } };
        return Accesses;
    }

    std::span<const ResourceAccess> TransformWorldSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 1> Accesses{ { { typeid(std::unordered_map<Arche::EntityID, SimpleMath::Matrix>), Access::Write } } };
        return Accesses;
    }

    void TransformWorldSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        Ctx.WorldMatrices.clear();

        std::vector<PendingEntity> PendingEntities{};
        for (auto [TransformComponent, HierarchyComponent] : World.Query<Transform, EntityHierarchy>()) {
            (void)TransformComponent;

            const EntityHierarchy* ParentHierarchyComponent{ World.GetComponent<EntityHierarchy>(HierarchyComponent.parent) };
            const Transform* ParentTransformComponent{ World.GetComponent<Transform>(HierarchyComponent.parent) };
            if (HierarchyComponent.parent != Arche::NullEntityID && ParentHierarchyComponent != nullptr && ParentTransformComponent != nullptr) {
                continue;
            }

            PendingEntity RootPendingEntity{};
            RootPendingEntity.EntityId = HierarchyComponent.self;
            RootPendingEntity.ParentWorldMatrix = SimpleMath::Matrix::Identity;
            PendingEntities.push_back(RootPendingEntity);
        }

        while (PendingEntities.empty() == false) {
            const PendingEntity CurrentPendingEntity{ PendingEntities.back() };
            PendingEntities.pop_back();

            Transform* TransformComponent{ World.GetComponent<Transform>(CurrentPendingEntity.EntityId) };
            const EntityHierarchy* HierarchyComponent{ World.GetComponent<EntityHierarchy>(CurrentPendingEntity.EntityId) };
            if (TransformComponent == nullptr || HierarchyComponent == nullptr) {
                continue;
            }

            const SimpleMath::Matrix LocalWorldMatrix{ BuildLocalWorldMatrix(*TransformComponent) };
            const SimpleMath::Matrix WorldMatrix{ LocalWorldMatrix * CurrentPendingEntity.ParentWorldMatrix };
            TransformComponent->worldMatrix = WorldMatrix;
            Ctx.WorldMatrices[CurrentPendingEntity.EntityId] = WorldMatrix;

            Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
            while (ChildEntityId != Arche::NullEntityID) {
                const EntityHierarchy* ChildHierarchyComponent{ World.GetComponent<EntityHierarchy>(ChildEntityId) };
                if (ChildHierarchyComponent == nullptr) {
                    break;
                }

                if (World.GetComponent<Transform>(ChildEntityId) != nullptr) {
                    PendingEntity ChildPendingEntity{};
                    ChildPendingEntity.EntityId = ChildEntityId;
                    ChildPendingEntity.ParentWorldMatrix = WorldMatrix;
                    PendingEntities.push_back(ChildPendingEntity);
                }

                ChildEntityId = ChildHierarchyComponent->nextSibling;
            }
        }
    }
}
