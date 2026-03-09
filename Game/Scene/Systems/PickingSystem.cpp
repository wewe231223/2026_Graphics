#include "PickingSystem.h"

#include <array>
#include <limits>
#include <DirectXMath.h>
#include "Core/Config.h"
#include "Game/Base/Input.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Utility/SimpleMathWrapper.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Transform.h"

#ifdef max 
#undef max 
#endif 

namespace {
    SimpleMath::Matrix BuildLocalWorldMatrix(const Game::Transform& TransformComponent) {
        const SimpleMath::Matrix TrsMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
        return TransformComponent.nodeToParent * TrsMatrix;
    }

    bool TryFindActiveCamera(Arche::World& World, const Game::Transform*& OutTransform, const Game::Camera*& OutCamera) {
        for (const auto [TransformComponent, CameraComponent] : World.Query<Game::Transform, Game::Camera>()) {
            if (CameraComponent.isActive == false) {
                continue;
            }

            OutTransform = &TransformComponent;
            OutCamera = &CameraComponent;
            return true;
        }

        return false;
    }

    bool TryBuildPickingRay(const Game::Transform& CameraTransform, const Game::Camera& CameraComponent, DirectX::XMVECTOR& OutRayOrigin, DirectX::XMVECTOR& OutRayDirection) {
        const Globals::Input& Input{ Globals::Input::Get() };
        const DirectX::Mouse::State& MouseState{ Input.GetMouseState() };
        const float ViewWidth{ static_cast<float>(Config::Query()->Get<int>("Window_Width")) };
        const float ViewHeight{ static_cast<float>(Config::Query()->Get<int>("Window_Height")) };

        if (ViewWidth <= 0.0f || ViewHeight <= 0.0f) {
            return false;
        }

        const SimpleMath::Vector3 NearPoint{ SimpleMath::Unproject(SimpleMath::Vector3{ static_cast<float>(MouseState.x), static_cast<float>(MouseState.y), 0.0f }, 0.0f, 0.0f, ViewWidth, ViewHeight, 0.0f, 1.0f, CameraComponent.projMatrix, CameraComponent.viewMatrix, SimpleMath::Matrix::Identity) };
        const SimpleMath::Vector3 FarPoint{ SimpleMath::Unproject(SimpleMath::Vector3{ static_cast<float>(MouseState.x), static_cast<float>(MouseState.y), 1.0f }, 0.0f, 0.0f, ViewWidth, ViewHeight, 0.0f, 1.0f, CameraComponent.projMatrix, CameraComponent.viewMatrix, SimpleMath::Matrix::Identity) };
        SimpleMath::Vector3 Direction{ FarPoint - NearPoint };

        if (Direction.LengthSquared() <= 0.0f) {
            return false;
        }

        Direction.Normalize();
        OutRayOrigin = DirectX::XMLoadFloat3(&CameraTransform.position);
        OutRayDirection = DirectX::XMLoadFloat3(&Direction);
        return true;
    }

    void ResolvePickedEntityRecursive(Arche::World& World, Arche::EntityID EntityId, const SimpleMath::Matrix& ParentWorld, DirectX::FXMVECTOR RayOrigin, DirectX::FXMVECTOR RayDirection, float& InOutNearestDistance, Arche::EntityID& InOutPickedEntityId) {
        const Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(EntityId) };
        const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(EntityId) };

        if (TransformComponent == nullptr || HierarchyComponent == nullptr) {
            return;
        }

        const SimpleMath::Matrix NodeWorld{ BuildLocalWorldMatrix(*TransformComponent) * ParentWorld };
        const Game::BoundingBox* BoundingBoxComponent{ World.GetComponent<Game::BoundingBox>(EntityId) };

        if (BoundingBoxComponent != nullptr) {
            DirectX::BoundingOrientedBox WorldBoundingBox{};
            BoundingBoxComponent->GetObb().Transform(WorldBoundingBox, NodeWorld);

            float HitDistance{ 0.0f };
            const bool IsIntersects{ WorldBoundingBox.Intersects(RayOrigin, RayDirection, HitDistance) };
            if (IsIntersects && HitDistance < InOutNearestDistance) {
                InOutNearestDistance = HitDistance;
                InOutPickedEntityId = EntityId;
            }
        }

        Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
        while (ChildEntityId != Arche::NullEntityID) {
            const Game::EntityHierarchy* ChildHierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(ChildEntityId) };
            if (ChildHierarchyComponent == nullptr) {
                break;
            }

            ResolvePickedEntityRecursive(World, ChildEntityId, NodeWorld, RayOrigin, RayDirection, InOutNearestDistance, InOutPickedEntityId);
            ChildEntityId = ChildHierarchyComponent->nextSibling;
        }
    }
}

namespace Game {
    const std::string& PickingSystem::Name() const {
        return mName;
    }

    Phase PickingSystem::GetPhase() const {
        return Phase::Update;
    }

    std::span<const ComponentAccess> PickingSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 4> Accesses{ { { typeid(Transform), Access::Read }, { typeid(Camera), Access::Read }, { typeid(EntityHierarchy), Access::Read }, { typeid(BoundingBox), Access::Read } } };
        return Accesses;
    }

    std::span<const ResourceAccess> PickingSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 0> Accesses{};
        return Accesses;
    }

    void PickingSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        const Globals::Input& Input{ Globals::Input::Get() };
        const DirectX::Mouse::ButtonStateTracker& MouseTracker{ Input.GetMouseTracker() };
        if (MouseTracker.leftButton != DirectX::Mouse::ButtonStateTracker::PRESSED) {
            return;
        }

        const Transform* CameraTransform{ nullptr };
        const Camera* CameraComponent{ nullptr };
        if (TryFindActiveCamera(World, CameraTransform, CameraComponent) == false || CameraTransform == nullptr || CameraComponent == nullptr) {
            Ctx.PickedEntityId = Arche::NullEntityID;
            return;
        }

        DirectX::XMVECTOR RayOrigin{};
        DirectX::XMVECTOR RayDirection{};
        if (TryBuildPickingRay(*CameraTransform, *CameraComponent, RayOrigin, RayDirection) == false) {
            Ctx.PickedEntityId = Arche::NullEntityID;
            return;
        }

        float NearestDistance{ std::numeric_limits<float>::max() };
        Arche::EntityID PickedEntityId{ Arche::NullEntityID };

        for (const auto [TransformComponent, HierarchyComponent] : World.Query<Transform, EntityHierarchy>()) {
            (void)TransformComponent;

            if (HierarchyComponent.parent != Arche::NullEntityID) {
                continue;
            }

            ResolvePickedEntityRecursive(World, HierarchyComponent.self, SimpleMath::Matrix::Identity, RayOrigin, RayDirection, NearestDistance, PickedEntityId);
        }

        Ctx.PickedEntityId = PickedEntityId;
    }
}