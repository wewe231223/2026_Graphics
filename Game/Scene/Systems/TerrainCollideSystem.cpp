#include "Game/Scene/Systems/TerrainCollideSystem.h"

#include <array>
#include <utility>
#include "Game/Scene/Components/TerrainCollider.h"
#include "Game/Scene/Components/TerrainCollidee.h"
#include "Game/Scene/Components/Transform.h"

namespace Game {
    TerrainCollideSystem::TerrainCollideSystem()
        : mName{ "TerrainCollideSystem" } {
    }

    TerrainCollideSystem::~TerrainCollideSystem() {
    }

    TerrainCollideSystem::TerrainCollideSystem(const TerrainCollideSystem& Other)
        : mName{ Other.mName } {
    }

    TerrainCollideSystem& TerrainCollideSystem::operator=(const TerrainCollideSystem& Other) {
        if (this == &Other) {
            return *this;
        }

        mName = Other.mName;
        return *this;
    }

    TerrainCollideSystem::TerrainCollideSystem(TerrainCollideSystem&& Other) noexcept
        : mName{ std::move(Other.mName) } {
    }

    TerrainCollideSystem& TerrainCollideSystem::operator=(TerrainCollideSystem&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mName = std::move(Other.mName);
        return *this;
    }

    const std::string& TerrainCollideSystem::Name() const {
        return mName;
    }

    Phase TerrainCollideSystem::GetPhase() const {
        return Phase::PostUpdate;
    }

    std::span<const ComponentAccess> TerrainCollideSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 3> Accesses{ {
            { typeid(TerrainCollider), Access::Read },
            { typeid(TerrainCollidee), Access::Read },
            { typeid(Transform), Access::Write }
        } };
        return Accesses;
    }

    std::span<const ResourceAccess> TerrainCollideSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 0> Accesses{};
        return Accesses;
    }

    void TerrainCollideSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Ctx;
        (void)Dt;

        for (auto [TerrainColliderComponent, TransformComponent] : World.Query<TerrainCollider, Transform>()) {
            if (TerrainColliderComponent.mTerrainCollide == false) {
                continue;
            }

            SimpleMath::Vector3 AdjustedPosition{ TransformComponent.position };
            bool IsResolved{};
            for (const auto [TerrainCollideeComponent] : World.Query<TerrainCollidee>()) {
                TerrainHeightResolver* TerrainHeightResolverPointer{ TerrainCollideeComponent.mTerrainHeightResolver };
                if (TerrainHeightResolverPointer == nullptr) {
                    continue;
                }

                SimpleMath::Vector3 CandidatePosition{ TransformComponent.position };
                const bool IsInsideTerrainBounds{ TerrainHeightResolverPointer->TryResolvePositionY(CandidatePosition) };
                if (IsInsideTerrainBounds == false) {
                    continue;
                }

                AdjustedPosition = CandidatePosition;
                IsResolved = true;
                break;
            }

            if (IsResolved == true) {
                TransformComponent.position = AdjustedPosition;
            }
        }
    }
}
