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

    bool AreMatricesEqual(const SimpleMath::Matrix& Left, const SimpleMath::Matrix& Right) {
        return Left._11 == Right._11 && Left._12 == Right._12 && Left._13 == Right._13 && Left._14 == Right._14 && Left._21 == Right._21 && Left._22 == Right._22 && Left._23 == Right._23 && Left._24 == Right._24 && Left._31 == Right._31 && Left._32 == Right._32 && Left._33 == Right._33 && Left._34 == Right._34 && Left._41 == Right._41 && Left._42 == Right._42 && Left._43 == Right._43 && Left._44 == Right._44;
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
            Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(CurrentPathEntityId) };
            if (TransformComponent == nullptr) {
                return false;
            }

            const SimpleMath::Matrix LocalWorldMatrix{ BuildLocalWorldMatrix(*TransformComponent) };
            const SimpleMath::Matrix CurrentWorldMatrix{ LocalWorldMatrix * ParentWorldMatrix };
            TransformComponent->mPrevWorldMatrix = TransformComponent->mWorldMatrixCacheValid == true ? TransformComponent->worldMatrix : CurrentWorldMatrix;
            TransformComponent->mWorldMatrixChanged = TransformComponent->mWorldMatrixCacheValid == false || AreMatricesEqual(TransformComponent->worldMatrix, CurrentWorldMatrix) == false;
            TransformComponent->mWorldMatrixCacheValid = true;
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
