#include "PickingSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <DirectXMath.h>
#include "Core/Config.h"
#include "Game/Base/Input.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Utility/SimpleMathWrapper.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/PickingGizmo.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Events/SelectionEvent.h"
#include "Core/Event/EventQueue.h"

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

    void DeactivatePickingGizmos(Arche::World& World) {
        for (auto [MeshRenderer, Gizmo] : World.Query<Game::StaticMeshRenderer, Game::PickingGizmo>()) {
            (void)Gizmo;
            MeshRenderer.active = false;
        }
    }

    bool TryResolveEntityWorldAndBounds(Arche::World& World, Arche::EntityID EntityId, SimpleMath::Matrix& OutWorld, DirectX::BoundingOrientedBox& OutWorldBounds) {
        const Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(EntityId) };
        const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(EntityId) };
        const Game::BoundingBox* BoundingBoxComponent{ World.GetComponent<Game::BoundingBox>(EntityId) };
        if (TransformComponent == nullptr || HierarchyComponent == nullptr || BoundingBoxComponent == nullptr) {
            return false;
        }

        SimpleMath::Matrix WorldMatrix{ BuildLocalWorldMatrix(*TransformComponent) };
        Arche::EntityID CurrentParentId{ HierarchyComponent->parent };
        while (CurrentParentId != Arche::NullEntityID) {
            const Game::Transform* ParentTransform{ World.GetComponent<Game::Transform>(CurrentParentId) };
            const Game::EntityHierarchy* ParentHierarchy{ World.GetComponent<Game::EntityHierarchy>(CurrentParentId) };
            if (ParentTransform == nullptr || ParentHierarchy == nullptr) {
                return false;
            }

            WorldMatrix = WorldMatrix * BuildLocalWorldMatrix(*ParentTransform);
            CurrentParentId = ParentHierarchy->parent;
        }

        OutWorld = WorldMatrix;
        BoundingBoxComponent->GetObb().Transform(OutWorldBounds, WorldMatrix);
        return true;
    }

    bool TryResolveAxisRayParameter(const SimpleMath::Vector3& RayOrigin, const SimpleMath::Vector3& RayDirection, const SimpleMath::Vector3& AxisOrigin, const SimpleMath::Vector3& AxisDirection, float& OutAxisParameter) {
        const float AxisDotAxis{ AxisDirection.Dot(AxisDirection) };
        const float RayDotRay{ RayDirection.Dot(RayDirection) };
        const float AxisDotRay{ AxisDirection.Dot(RayDirection) };
        const SimpleMath::Vector3 OriginDelta{ AxisOrigin - RayOrigin };
        const float AxisDotOriginDelta{ AxisDirection.Dot(OriginDelta) };
        const float RayDotOriginDelta{ RayDirection.Dot(OriginDelta) };
        const float Denominator{ AxisDotAxis * RayDotRay - AxisDotRay * AxisDotRay };
        if (std::fabs(Denominator) <= 0.00001f) {
            return false;
        }

        OutAxisParameter = (AxisDotOriginDelta * RayDotRay - RayDotOriginDelta * AxisDotRay) / Denominator;
        return true;
    }

    bool TryApplyTargetWorldPosition(Arche::World& World, Arche::EntityID TargetEntityId, const SimpleMath::Vector3& TargetWorldPosition) {
        Game::Transform* TargetTransform{ World.GetComponent<Game::Transform>(TargetEntityId) };
        const Game::EntityHierarchy* TargetHierarchy{ World.GetComponent<Game::EntityHierarchy>(TargetEntityId) };
        if (TargetTransform == nullptr || TargetHierarchy == nullptr) {
            return false;
        }

        SimpleMath::Matrix ParentWorldMatrix{ SimpleMath::Matrix::Identity };
        Arche::EntityID CurrentParentId{ TargetHierarchy->parent };
        while (CurrentParentId != Arche::NullEntityID) {
            const Game::Transform* ParentTransform{ World.GetComponent<Game::Transform>(CurrentParentId) };
            const Game::EntityHierarchy* ParentHierarchy{ World.GetComponent<Game::EntityHierarchy>(CurrentParentId) };
            if (ParentTransform == nullptr || ParentHierarchy == nullptr) {
                return false;
            }

            ParentWorldMatrix = ParentWorldMatrix * BuildLocalWorldMatrix(*ParentTransform);
            CurrentParentId = ParentHierarchy->parent;
        }

        const SimpleMath::Matrix ParentWorldInverseMatrix{ ParentWorldMatrix.Invert() };
        TargetTransform->position = SimpleMath::Vector3::Transform(TargetWorldPosition, ParentWorldInverseMatrix);
        return true;
    }

    void UpdatePickingGizmos(Arche::World& World, Arche::EntityID PickedEntityId) {
        DeactivatePickingGizmos(World);
        if (PickedEntityId == Arche::NullEntityID) {
            return;
        }

        SimpleMath::Matrix PickedWorldMatrix{ SimpleMath::Matrix::Identity };
        DirectX::BoundingOrientedBox PickedWorldBounds{};
        if (TryResolveEntityWorldAndBounds(World, PickedEntityId, PickedWorldMatrix, PickedWorldBounds) == false) {
            return;
        }

        const SimpleMath::Vector3 BoundsCenter{ PickedWorldBounds.Center.x, PickedWorldBounds.Center.y, PickedWorldBounds.Center.z };
        const float ExtentsArray[3]{ PickedWorldBounds.Extents.x, PickedWorldBounds.Extents.y, PickedWorldBounds.Extents.z };
        const float MaxExtent{ std::max(ExtentsArray[0], std::max(ExtentsArray[1], ExtentsArray[2])) };
        const float Thickness{ std::max(0.05f, MaxExtent * 0.12f) };
        const float Length{ std::max(0.35f, MaxExtent * 0.6f) };
        const float Gap{ std::max(0.05f, MaxExtent * 0.08f) };

        const SimpleMath::Quaternion AxisOrientation{ PickedWorldBounds.Orientation.x, PickedWorldBounds.Orientation.y, PickedWorldBounds.Orientation.z, PickedWorldBounds.Orientation.w };
        const std::array<SimpleMath::Vector3, 3> AxisDirections{ {
            SimpleMath::Vector3::Transform(SimpleMath::Vector3::UnitX, AxisOrientation),
            SimpleMath::Vector3::Transform(SimpleMath::Vector3::UnitY, AxisOrientation),
            SimpleMath::Vector3::Transform(SimpleMath::Vector3::UnitZ, AxisOrientation)
        } };

        for (auto [MeshRenderer, TransformComponent, Gizmo] : World.Query<Game::StaticMeshRenderer, Game::Transform, Game::PickingGizmo>()) {
            if (Gizmo.axisIndex >= AxisDirections.size()) {
                continue;
            }

            const SimpleMath::Vector3 AxisDirection{ AxisDirections[Gizmo.axisIndex] };
            const float StartOffset{ ExtentsArray[Gizmo.axisIndex] + Gap + Thickness * 0.5f };
            const float Offset{ StartOffset + Length * 0.5f };

            TransformComponent.position = BoundsCenter + AxisDirection * (Offset * Gizmo.directionSign);
            TransformComponent.rotation = AxisOrientation;
            TransformComponent.rotationEuler = SimpleMath::Vector3{};

            if (Gizmo.axisIndex == 0) {
                TransformComponent.scale = SimpleMath::Vector3{ Length, Thickness, Thickness };
            }
            else if (Gizmo.axisIndex == 1) {
                TransformComponent.scale = SimpleMath::Vector3{ Thickness, Length, Thickness };
            }
            else {
                TransformComponent.scale = SimpleMath::Vector3{ Thickness, Thickness, Length };
            }

            MeshRenderer.active = true;
        }
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
        static std::array<ComponentAccess, 6> Accesses{ { { typeid(Transform), Access::Write }, { typeid(Camera), Access::Read }, { typeid(EntityHierarchy), Access::Read }, { typeid(BoundingBox), Access::Read }, { typeid(StaticMeshRenderer), Access::Write }, { typeid(PickingGizmo), Access::Read } } };
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
        const bool IsMousePickRequested{ MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::PRESSED };
        const bool IsMouseDragActive{ MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::PRESSED || MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::HELD };
        const bool IsMouseDragReleased{ MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::RELEASED || MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::UP };
        const Arche::EntityID PreviouslyPickedEntityId{ Ctx.PickedEntityId };

        if (IsMousePickRequested) {
            const Transform* CameraTransform{ nullptr };
            const Camera* CameraComponent{ nullptr };
            if (TryFindActiveCamera(World, CameraTransform, CameraComponent) == false || CameraTransform == nullptr || CameraComponent == nullptr) {
                Ctx.PickedEntityId = Arche::NullEntityID;

                Game::PickedEntityChangedPayload PickedEntityChangedPayload{};
                PickedEntityChangedPayload.PickedEntityId = Arche::NullEntityID;
                Core::Event::Enqueue<Game::PickedEntityChangedEventTag, Game::PickedEntityChangedPayload>(std::move(PickedEntityChangedPayload), true);
            }
            else {
                DirectX::XMVECTOR RayOrigin{};
                DirectX::XMVECTOR RayDirection{};
                if (TryBuildPickingRay(*CameraTransform, *CameraComponent, RayOrigin, RayDirection) == false) {
                    Ctx.PickedEntityId = Arche::NullEntityID;

                    Game::PickedEntityChangedPayload PickedEntityChangedPayload{};
                    PickedEntityChangedPayload.PickedEntityId = Arche::NullEntityID;
                    Core::Event::Enqueue<Game::PickedEntityChangedEventTag, Game::PickedEntityChangedPayload>(std::move(PickedEntityChangedPayload), true);
                }
                else {
                    float NearestDistance{ std::numeric_limits<float>::max() };
                    Arche::EntityID PickedEntityId{ Arche::NullEntityID };

                    for (const auto [TransformComponent, HierarchyComponent] : World.Query<Transform, EntityHierarchy>()) {
                        (void)TransformComponent;

                        if (HierarchyComponent.parent != Arche::NullEntityID) {
                            continue;
                        }

                        ResolvePickedEntityRecursive(World, HierarchyComponent.self, SimpleMath::Matrix::Identity, RayOrigin, RayDirection, NearestDistance, PickedEntityId);
                    }

                    const PickingGizmo* PickedGizmo{ World.GetComponent<PickingGizmo>(PickedEntityId) };
                    if (PickedGizmo != nullptr) {
                        mIsGizmoDragging = false;
                        mDraggingTargetEntityId = Arche::NullEntityID;

                        Arche::EntityID DragTargetEntityId{ PreviouslyPickedEntityId };
                        if (DragTargetEntityId != Arche::NullEntityID && World.GetComponent<PickingGizmo>(DragTargetEntityId) != nullptr) {
                            DragTargetEntityId = Arche::NullEntityID;
                        }

                        Ctx.PickedEntityId = DragTargetEntityId;

                        if (DragTargetEntityId != Arche::NullEntityID) {
                            SimpleMath::Matrix TargetWorldMatrix{ SimpleMath::Matrix::Identity };
                            DirectX::BoundingOrientedBox TargetWorldBounds{};
                            if (TryResolveEntityWorldAndBounds(World, DragTargetEntityId, TargetWorldMatrix, TargetWorldBounds)) {
                                const Transform* DragCameraTransform{ nullptr };
                                const Camera* DragCameraComponent{ nullptr };
                                DirectX::XMVECTOR DragRayOriginVector{};
                                DirectX::XMVECTOR DragRayDirectionVector{};
                                if (TryFindActiveCamera(World, DragCameraTransform, DragCameraComponent) && DragCameraTransform != nullptr && DragCameraComponent != nullptr && TryBuildPickingRay(*DragCameraTransform, *DragCameraComponent, DragRayOriginVector, DragRayDirectionVector)) {
                                    SimpleMath::Vector3 DragRayOrigin{};
                                    SimpleMath::Vector3 DragRayDirection{};
                                    DirectX::XMStoreFloat3(&DragRayOrigin, DragRayOriginVector);
                                    DirectX::XMStoreFloat3(&DragRayDirection, DragRayDirectionVector);

                                    const SimpleMath::Quaternion AxisOrientation{ TargetWorldBounds.Orientation.x, TargetWorldBounds.Orientation.y, TargetWorldBounds.Orientation.z, TargetWorldBounds.Orientation.w };
                                    const std::array<SimpleMath::Vector3, 3> AxisDirections{ { SimpleMath::Vector3::Transform(SimpleMath::Vector3::UnitX, AxisOrientation), SimpleMath::Vector3::Transform(SimpleMath::Vector3::UnitY, AxisOrientation), SimpleMath::Vector3::Transform(SimpleMath::Vector3::UnitZ, AxisOrientation) } };
                                    if (PickedGizmo->axisIndex < AxisDirections.size()) {
                                        const SimpleMath::Vector3 AxisDirection{ AxisDirections[PickedGizmo->axisIndex] * PickedGizmo->directionSign };
                                        const SimpleMath::Vector3 AxisOrigin{ TargetWorldBounds.Center.x, TargetWorldBounds.Center.y, TargetWorldBounds.Center.z };
                                        float AxisParameter{ 0.0f };
                                        if (TryResolveAxisRayParameter(DragRayOrigin, DragRayDirection, AxisOrigin, AxisDirection, AxisParameter)) {
                                            mIsGizmoDragging = true;
                                            mDraggingTargetEntityId = DragTargetEntityId;
                                            mDraggingStartAxisParameter = AxisParameter;
                                            mDraggingStartWorldPosition = TargetWorldMatrix.Translation();
                                            mDraggingAxisOrigin = AxisOrigin;
                                            mDraggingAxisDirection = AxisDirection;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    else {
                        mIsGizmoDragging = false;
                        mDraggingTargetEntityId = Arche::NullEntityID;
                        Ctx.PickedEntityId = PickedEntityId;

                        Game::PickedEntityChangedPayload PickedEntityChangedPayload{};
                        PickedEntityChangedPayload.PickedEntityId = PickedEntityId;
                        Core::Event::Enqueue<Game::PickedEntityChangedEventTag, Game::PickedEntityChangedPayload>(std::move(PickedEntityChangedPayload), true);
                    }
                }
            }
        }

        if (mIsGizmoDragging && IsMouseDragActive && mDraggingTargetEntityId != Arche::NullEntityID) {
            const Transform* CameraTransform{ nullptr };
            const Camera* CameraComponent{ nullptr };
            DirectX::XMVECTOR RayOriginVector{};
            DirectX::XMVECTOR RayDirectionVector{};
            if (TryFindActiveCamera(World, CameraTransform, CameraComponent) && CameraTransform != nullptr && CameraComponent != nullptr && TryBuildPickingRay(*CameraTransform, *CameraComponent, RayOriginVector, RayDirectionVector)) {
                SimpleMath::Vector3 RayOrigin{};
                SimpleMath::Vector3 RayDirection{};
                DirectX::XMStoreFloat3(&RayOrigin, RayOriginVector);
                DirectX::XMStoreFloat3(&RayDirection, RayDirectionVector);

                float CurrentAxisParameter{ 0.0f };
                if (TryResolveAxisRayParameter(RayOrigin, RayDirection, mDraggingAxisOrigin, mDraggingAxisDirection, CurrentAxisParameter)) {
                    const float DragDelta{ mDraggingStartAxisParameter - CurrentAxisParameter };
                    const SimpleMath::Vector3 TargetWorldPosition{ mDraggingStartWorldPosition + mDraggingAxisDirection * DragDelta };
                    if (TryApplyTargetWorldPosition(World, mDraggingTargetEntityId, TargetWorldPosition)) {
                        Ctx.PickedEntityId = mDraggingTargetEntityId;
                        UpdatePickingGizmos(World, Ctx.PickedEntityId);
                    }
                }
            }
        }

        if (mIsGizmoDragging && IsMouseDragReleased) {
            mIsGizmoDragging = false;
            mDraggingTargetEntityId = Arche::NullEntityID;
        }

        if (mLastGizmoPickedEntityId != Ctx.PickedEntityId) {
            UpdatePickingGizmos(World, Ctx.PickedEntityId);
            mLastGizmoPickedEntityId = Ctx.PickedEntityId;
        }
    }



}
