#include "PhysicsActorUpdateSystem.h"

#include <array>
#include <cmath>
#include <string_view>
#include <vector>
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/PhysicsActor.h"
#include "Game/Scene/Components/Tags.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    constexpr std::string_view PlayerTagText{ "PlayerTag" };
    constexpr DirectX::SimpleMath::Vector4 PlayerDynamicCollisionHighlightColor{ 1.0f, 0.85f, 0.05f, 1.0f };
    constexpr float PlayerDynamicCollisionHighlightLineThickness{ 0.009f };
    constexpr float PlayerDynamicCollisionHighlightExtentPadding{ 0.06f };

    DirectX::SimpleMath::Vector3 ResolveObbCenter(const DirectX::BoundingOrientedBox& WorldObb) {
        return DirectX::SimpleMath::Vector3{ WorldObb.Center.x, WorldObb.Center.y, WorldObb.Center.z };
    }

    DirectX::SimpleMath::Vector3 ResolveHighlightObbExtents(const DirectX::BoundingOrientedBox& WorldObb) {
        return DirectX::SimpleMath::Vector3{ std::fabs(WorldObb.Extents.x) + PlayerDynamicCollisionHighlightExtentPadding, std::fabs(WorldObb.Extents.y) + PlayerDynamicCollisionHighlightExtentPadding, std::fabs(WorldObb.Extents.z) + PlayerDynamicCollisionHighlightExtentPadding };
    }

    DirectX::SimpleMath::Quaternion ResolveObbOrientation(const DirectX::BoundingOrientedBox& WorldObb) {
        return DirectX::SimpleMath::Quaternion{ WorldObb.Orientation.x, WorldObb.Orientation.y, WorldObb.Orientation.z, WorldObb.Orientation.w };
    }

    bool IntersectsAnyObb(const DirectX::BoundingOrientedBox& SourceObb, const std::vector<DirectX::BoundingOrientedBox>& TargetObbs) {
        for (const DirectX::BoundingOrientedBox& TargetObb : TargetObbs) {
            if (SourceObb.Intersects(TargetObb) == true) {
                return true;
            }
        }

        return false;
    }

    std::vector<DirectX::BoundingOrientedBox> CollectPlayerWorldObbs(Arche::World& World) {
        std::vector<DirectX::BoundingOrientedBox> PlayerWorldObbs{};
        for (auto [TagComponent, PhysicsActorComponent] : World.Query<Game::Tag, Game::PhysicsActor>()) {
            const std::string_view TagText{ Game::GetTagTextView(TagComponent) };
            if (TagText != PlayerTagText) {
                continue;
            }

            PhysicsActorBase* ActorPointer{ PhysicsActorComponent.mActorPointer };
            if (ActorPointer == nullptr || ActorPointer->GetIsActive() == false) {
                continue;
            }

            PlayerWorldObbs.push_back(ActorPointer->GetWorldBoundingBox());
        }

        return PlayerWorldObbs;
    }

    void AppendDynamicCollisionHighlight(Game::RFD::RenderFrameData& RenderData, const DirectX::BoundingOrientedBox& WorldObb) {
        const DirectX::SimpleMath::Vector3 Center{ ResolveObbCenter(WorldObb) };
        const DirectX::SimpleMath::Vector3 Extents{ ResolveHighlightObbExtents(WorldObb) };
        const DirectX::SimpleMath::Quaternion Orientation{ ResolveObbOrientation(WorldObb) };
        RenderData.debugGeometryContexts.push_back(Game::RFD::DebugGeometryContext::CreateWireCube(Center, Extents, Orientation, PlayerDynamicCollisionHighlightColor, PlayerDynamicCollisionHighlightLineThickness));
    }

    void AppendPlayerDynamicCollisionHighlights(Arche::World& World, Game::RFD::RenderFrameData& RenderData) {
        const bool IsDebugGeometryDrawEnabled{ (RenderData.globals.flags & Game::RFD::FrameGlobalFlagDrawDebugGeometry) != 0u };
        if (IsDebugGeometryDrawEnabled == false) {
            return;
        }

        const std::vector<DirectX::BoundingOrientedBox> PlayerWorldObbs{ CollectPlayerWorldObbs(World) };
        if (PlayerWorldObbs.empty() == true) {
            return;
        }

        for (auto [PhysicsActorComponent] : World.Query<Game::PhysicsActor>()) {
            PhysicsActorBase* ActorPointer{ PhysicsActorComponent.mActorPointer };
            if (ActorPointer == nullptr || ActorPointer->GetIsActive() == false) {
                continue;
            }

            if (ActorPointer->GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic) {
                continue;
            }

            const DirectX::BoundingOrientedBox& DynamicWorldObb{ ActorPointer->GetWorldBoundingBox() };
            if (IntersectsAnyObb(DynamicWorldObb, PlayerWorldObbs) == false) {
                continue;
            }

            AppendDynamicCollisionHighlight(RenderData, DynamicWorldObb);
        }
    }
}

namespace Game {
    const std::string& PhysicsActorUpdateSystem::Name() const {
        return mName;
    }

    Phase PhysicsActorUpdateSystem::GetPhase() const {
        return Phase::PhysicsActorUpdate;
    }

    std::span<const ComponentAccess> PhysicsActorUpdateSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 5> Accesses{ { { typeid(PhysicsActor), Access::Read }, { typeid(Transform), Access::Write }, { typeid(EntityHierarchy), Access::Read }, { typeid(BoundingBox), Access::Write }, { typeid(Tag), Access::Read } } };
        return Accesses;
    }

    std::span<const ResourceAccess> PhysicsActorUpdateSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 2> Accesses{ { { typeid(IPhysicsWorld), Access::Read }, { typeid(RFD::RenderFrameData), Access::Write } } };
        return Accesses;
    }

    void PhysicsActorUpdateSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        const IPhysicsWorld* PhysicsWorldResource{ Ctx.PhysicsWorldResource };
        if (PhysicsWorldResource == nullptr) {
            return;
        }

        for (auto [PhysicsActorComponent, TransformComponent, EntityHierarchyComponent] : World.Query<PhysicsActor, Transform, EntityHierarchy>()) {
            PhysicsActorBase* ActorPointer{ PhysicsActorComponent.mActorPointer };
            if (ActorPointer == nullptr) {
                continue;
            }

            DirectX::SimpleMath::Vector3 PhysicsPosition{ ActorPointer->GetPosition() };
            DirectX::SimpleMath::Quaternion PhysicsOrientation{ ActorPointer->GetOrientation() };
            DirectX::SimpleMath::Vector3 PhysicsScale{ ActorPointer->GetScale() };
            if (ActorPointer->GetActorType() == PhysicsActorBase::PhysicsActorType::Dynamic) {
                static_cast<void>(PhysicsWorldResource->TryGetInterpolatedActorTransform(*ActorPointer, PhysicsPosition, PhysicsOrientation, PhysicsScale));
            }

            TransformComponent.position = PhysicsPosition;
            TransformComponent.rotation = PhysicsOrientation;
            TransformComponent.scale = PhysicsScale;
            TransformComponent.UpdateEulerRadiansFromRotation();

            BoundingBox* BoundingBoxComponent{ World.GetComponent<BoundingBox>(EntityHierarchyComponent.self) };
            if (BoundingBoxComponent != nullptr) {
                BoundingBoxComponent->SetWorldObb(ActorPointer->GetWorldBoundingBox());
            }
        }

        AppendPlayerDynamicCollisionHighlights(World, Ctx.RenderData);
    }
}
