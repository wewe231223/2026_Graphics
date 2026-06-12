#include "TransformWorldSystem.h"

#include <array>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    SimpleMath::Matrix BuildLocalWorldMatrix(const Game::Transform& TransformComponent) {
        const SimpleMath::Matrix TrsMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
        return TransformComponent.nodeToParent * TrsMatrix;
    }

    bool TryResolveWorldMatrix(Arche::World& World, Arche::EntityID EntityId, std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& InOutWorldMatrices, SimpleMath::Matrix& OutWorldMatrix) {
        const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CachedWorldMatrixIter{ InOutWorldMatrices.find(EntityId) };
        if (CachedWorldMatrixIter != InOutWorldMatrices.end()) {
            OutWorldMatrix = CachedWorldMatrixIter->second;
            return true;
        }

        std::vector<Arche::EntityID> EntityPath{};
        Arche::EntityID CurrentEntityId{ EntityId };
        SimpleMath::Matrix ParentWorldMatrix{ SimpleMath::Matrix::Identity };

        while (CurrentEntityId != Arche::NullEntityID) {
            const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>::const_iterator CurrentCachedWorldMatrixIter{ InOutWorldMatrices.find(CurrentEntityId) };
            if (CurrentCachedWorldMatrixIter != InOutWorldMatrices.end()) {
                ParentWorldMatrix = CurrentCachedWorldMatrixIter->second;
                break;
            }

            const Game::Transform* TransformComponent{ std::as_const(World).GetComponent<Game::Transform>(CurrentEntityId) };
            const Game::EntityHierarchy* HierarchyComponent{ std::as_const(World).GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (TransformComponent == nullptr || HierarchyComponent == nullptr) {
                return false;
            }

            EntityPath.push_back(CurrentEntityId);
            CurrentEntityId = HierarchyComponent->parent;
        }

        for (std::vector<Arche::EntityID>::const_reverse_iterator EntityPathIter{ EntityPath.crbegin() }; EntityPathIter != EntityPath.crend(); ++EntityPathIter) {
            const Arche::EntityID CurrentPathEntityId{ *EntityPathIter };
            const Game::Transform* TransformComponent{ std::as_const(World).GetComponent<Game::Transform>(CurrentPathEntityId) };
            if (TransformComponent == nullptr) {
                return false;
            }

            const SimpleMath::Matrix LocalWorldMatrix{ BuildLocalWorldMatrix(*TransformComponent) };
            const SimpleMath::Matrix CurrentWorldMatrix{ LocalWorldMatrix * ParentWorldMatrix };
            InOutWorldMatrices[CurrentPathEntityId] = CurrentWorldMatrix;
            ParentWorldMatrix = CurrentWorldMatrix;
        }

        OutWorldMatrix = ParentWorldMatrix;
        return true;
    }

    void UpdateWorldMatrices(Arche::World& World) {
        std::unordered_map<Arche::EntityID, SimpleMath::Matrix> WorldMatrices{};
        for (auto [TransformComponent, HierarchyComponent] : World.Query<Game::Transform, Game::EntityHierarchy>()) {
            const Arche::EntityID EntityId{ HierarchyComponent.self };
            SimpleMath::Matrix WorldMatrix{};
            const bool IsWorldMatrixResolved{ TryResolveWorldMatrix(World, EntityId, WorldMatrices, WorldMatrix) };
            if (IsWorldMatrixResolved == false) {
                continue;
            }

            TransformComponent.worldMatrix = WorldMatrix;
        }
    }
}

namespace Game {
    TransformWorldSystem::TransformWorldSystem() {
    }

    TransformWorldSystem::~TransformWorldSystem() {
    }

    TransformWorldSystem::TransformWorldSystem(const TransformWorldSystem& Other)
        : mName{ Other.mName } {
    }

    TransformWorldSystem& TransformWorldSystem::operator=(const TransformWorldSystem& Other) {
        (void)Other;
        return *this;
    }

    TransformWorldSystem::TransformWorldSystem(TransformWorldSystem&& Other) noexcept
        : mName{ std::move(Other.mName) } {
    }

    TransformWorldSystem& TransformWorldSystem::operator=(TransformWorldSystem&& Other) noexcept {
        (void)Other;
        return *this;
    }

    const std::string& TransformWorldSystem::Name() const {
        return mName;
    }

    Phase TransformWorldSystem::GetPhase() const {
        return Phase::TransformWorld;
    }

    std::span<const ComponentAccess> TransformWorldSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 2> Accesses{ { { typeid(Transform), Access::Write }, { typeid(EntityHierarchy), Access::Read } } };
        return Accesses;
    }

    std::span<const ResourceAccess> TransformWorldSystem::ResourceAccesses() const {
        return {};
    }

    void TransformWorldSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Ctx;
        (void)Dt;

        UpdateWorldMatrices(World);
    }
}
