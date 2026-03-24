#include "PickingSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <unordered_map>
#include <DirectXMath.h>
#include "Core/Config.h"
#include "Game/Base/Input.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Bone.h"
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

    bool TryResolveEntityWorldMatrix(const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& WorldMatrices, Arche::EntityID EntityId, SimpleMath::Matrix& OutWorld) {
        const auto FoundWorldMatrix{ WorldMatrices.find(EntityId) };
        if (FoundWorldMatrix == WorldMatrices.end()) {
            return false;
        }

        OutWorld = FoundWorldMatrix->second;
        return true;
    }

    bool TryResolveEntityWorldAndBounds(const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& WorldMatrices, Arche::World& World, Arche::EntityID EntityId, SimpleMath::Matrix& OutWorld, DirectX::BoundingOrientedBox& OutWorldBounds) {
        const Game::BoundingBox* BoundingBoxComponent{ World.GetComponent<Game::BoundingBox>(EntityId) };
        if (BoundingBoxComponent == nullptr) {
            return false;
        }

        if (TryResolveEntityWorldMatrix(WorldMatrices, EntityId, OutWorld) == false) {
            return false;
        }

        BoundingBoxComponent->GetObb().Transform(OutWorldBounds, OutWorld);
        return true;
    }

    bool TryResolveEntityWorldPosition(Arche::World& World, Arche::EntityID EntityId, SimpleMath::Vector3& OutWorldPosition) {
        const Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(EntityId) };
        const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(EntityId) };
        if (TransformComponent == nullptr || HierarchyComponent == nullptr) {
            return false;
        }

        OutWorldPosition = TransformComponent->position;
        Arche::EntityID CurrentParentId{ HierarchyComponent->parent };
        while (CurrentParentId != Arche::NullEntityID) {
            const Game::Transform* ParentTransform{ World.GetComponent<Game::Transform>(CurrentParentId) };
            const Game::EntityHierarchy* ParentHierarchy{ World.GetComponent<Game::EntityHierarchy>(CurrentParentId) };
            if (ParentTransform == nullptr || ParentHierarchy == nullptr) {
                return false;
            }

            OutWorldPosition += ParentTransform->position;
            CurrentParentId = ParentHierarchy->parent;
        }

        return true;
    }

    void AccumulateSubtreeBounds(const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& WorldMatrices, Arche::World& World, Arche::EntityID EntityId, bool& InOutHasBounds, SimpleMath::Vector3& InOutMinimum, SimpleMath::Vector3& InOutMaximum) {
        const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(EntityId) };
        if (HierarchyComponent == nullptr) {
            return;
        }

        const Game::BoundingBox* BoundingBoxComponent{ World.GetComponent<Game::BoundingBox>(EntityId) };
        if (BoundingBoxComponent != nullptr) {
            DirectX::BoundingOrientedBox WorldBoundingBox{};
            SimpleMath::Matrix EntityWorld{};
            if (TryResolveEntityWorldMatrix(WorldMatrices, EntityId, EntityWorld) == false) {
                return;
            }

            BoundingBoxComponent->GetObb().Transform(WorldBoundingBox, EntityWorld);

            std::array<SimpleMath::Vector3, 8> Corners{};
            WorldBoundingBox.GetCorners(reinterpret_cast<DirectX::XMFLOAT3*>(Corners.data()));

            for (const SimpleMath::Vector3& Corner : Corners) {
                if (InOutHasBounds == false) {
                    InOutMinimum = Corner;
                    InOutMaximum = Corner;
                    InOutHasBounds = true;
                    continue;
                }

                InOutMinimum = SimpleMath::Vector3::Min(InOutMinimum, Corner);
                InOutMaximum = SimpleMath::Vector3::Max(InOutMaximum, Corner);
            }
        }

        Arche::EntityID ChildEntityId{ HierarchyComponent->firstChild };
        while (ChildEntityId != Arche::NullEntityID) {
            const Game::EntityHierarchy* ChildHierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(ChildEntityId) };
            if (ChildHierarchyComponent == nullptr) {
                break;
            }

            AccumulateSubtreeBounds(WorldMatrices, World, ChildEntityId, InOutHasBounds, InOutMinimum, InOutMaximum);
            ChildEntityId = ChildHierarchyComponent->nextSibling;
        }
    }

    bool TryResolveMergedSubtreeBounds(const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& WorldMatrices, Arche::World& World, Arche::EntityID RootEntityId, SimpleMath::Vector3& OutCenter, SimpleMath::Vector3& OutExtents) {
        SimpleMath::Matrix RootWorld{};
        if (TryResolveEntityWorldMatrix(WorldMatrices, RootEntityId, RootWorld) == false) {
            return false;
        }

        bool HasBounds{ false };
        SimpleMath::Vector3 BoundsMinimum{};
        SimpleMath::Vector3 BoundsMaximum{};
        AccumulateSubtreeBounds(WorldMatrices, World, RootEntityId, HasBounds, BoundsMinimum, BoundsMaximum);
        if (HasBounds == false) {
            return false;
        }

        OutCenter = (BoundsMinimum + BoundsMaximum) * 0.5f;
        OutExtents = (BoundsMaximum - BoundsMinimum) * 0.5f;
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

    void UpdatePickingGizmos(const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& WorldMatrices, Arche::World& World, Arche::EntityID PickedEntityId, Arche::EntityID HoveredGizmoEntityId) {
        DeactivatePickingGizmos(World);
        if (PickedEntityId == Arche::NullEntityID) {
            return;
        }

        const Game::Bone* BoneComponent{ World.GetComponent<Game::Bone>(PickedEntityId) };
        SimpleMath::Vector3 BoundsCenter{};
        SimpleMath::Vector3 BoundsExtents{};
        SimpleMath::Quaternion AxisOrientation{ SimpleMath::Quaternion::Identity };

        if (BoneComponent != nullptr) {
            SimpleMath::Matrix BoneWorldMatrix{ SimpleMath::Matrix::Identity };
            if (TryResolveEntityWorldMatrix(WorldMatrices, PickedEntityId, BoneWorldMatrix) == false) {
                return;
            }

            BoundsCenter = BoneWorldMatrix.Translation();
            AxisOrientation = SimpleMath::Quaternion::CreateFromRotationMatrix(BoneWorldMatrix);
        }
        else {
            if (TryResolveMergedSubtreeBounds(WorldMatrices, World, PickedEntityId, BoundsCenter, BoundsExtents) == false && TryResolveEntityWorldPosition(World, PickedEntityId, BoundsCenter) == false) {
                return;
            }

            const Game::Transform* RootTransformComponent{ World.GetComponent<Game::Transform>(PickedEntityId) };
            AxisOrientation = RootTransformComponent == nullptr ? SimpleMath::Quaternion::Identity : RootTransformComponent->rotation;
        }

        const float Thickness{ 0.05f };
        const float Length{ 0.75f };
        const float Gap{ 0.06f };
        const float HoverScaleFactor{ 1.1f };

        const std::array<SimpleMath::Vector3, 3> AxisDirections{ {
            SimpleMath::Vector3::Transform(SimpleMath::Vector3::UnitX, AxisOrientation),
            SimpleMath::Vector3::Transform(SimpleMath::Vector3::UnitY, AxisOrientation),
            SimpleMath::Vector3::Transform(SimpleMath::Vector3::UnitZ, AxisOrientation)
        } };

        for (auto [MeshRenderer, TransformComponent, HierarchyComponent, Gizmo] : World.Query<Game::StaticMeshRenderer, Game::Transform, Game::EntityHierarchy, Game::PickingGizmo>()) {
            if (Gizmo.axisIndex >= AxisDirections.size()) {
                continue;
            }

            const SimpleMath::Vector3 AxisDirection{ AxisDirections[Gizmo.axisIndex] };
            const SimpleMath::Vector3 AbsoluteAxisDirection{ std::fabs(AxisDirection.x), std::fabs(AxisDirection.y), std::fabs(AxisDirection.z) };
            const float AxisBoundsExtent{ BoundsExtents.x * AbsoluteAxisDirection.x + BoundsExtents.y * AbsoluteAxisDirection.y + BoundsExtents.z * AbsoluteAxisDirection.z };
            const float Offset{ AxisBoundsExtent + Gap + Length * 0.5f };

            TransformComponent.position = BoundsCenter + AxisDirection * Offset;
            TransformComponent.rotation = AxisOrientation;
            TransformComponent.rotationEuler = SimpleMath::Vector3{};

            const bool IsHoveredGizmo{ HierarchyComponent.self == HoveredGizmoEntityId };
            const float CurrentHoverScale{ IsHoveredGizmo ? HoverScaleFactor : 1.0f };
            if (Gizmo.axisIndex == 0) {
                TransformComponent.scale = SimpleMath::Vector3{ Length, Thickness, Thickness } * CurrentHoverScale;
            }
            else if (Gizmo.axisIndex == 1) {
                TransformComponent.scale = SimpleMath::Vector3{ Thickness, Length, Thickness } * CurrentHoverScale;
            }
            else {
                TransformComponent.scale = SimpleMath::Vector3{ Thickness, Thickness, Length } * CurrentHoverScale;
            }

            MeshRenderer.active = true;
        }
    }

    void ResolvePickedEntityRecursive(const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& WorldMatrices, Arche::World& World, Arche::EntityID EntityId, DirectX::FXMVECTOR RayOrigin, DirectX::FXMVECTOR RayDirection, float& InOutNearestDistance, Arche::EntityID& InOutPickedEntityId) {
        const Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(EntityId) };
        const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(EntityId) };

        if (TransformComponent == nullptr || HierarchyComponent == nullptr) {
            return;
        }

        SimpleMath::Matrix NodeWorld{};
        if (TryResolveEntityWorldMatrix(WorldMatrices, EntityId, NodeWorld) == false) {
            return;
        }
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

            ResolvePickedEntityRecursive(WorldMatrices, World, ChildEntityId, RayOrigin, RayDirection, InOutNearestDistance, InOutPickedEntityId);
            ChildEntityId = ChildHierarchyComponent->nextSibling;
        }
    }

    Arche::EntityID ResolveTopLevelEntityId(Arche::World& World, Arche::EntityID EntityId) {
        if (EntityId == Arche::NullEntityID) {
            return Arche::NullEntityID;
        }

        Arche::EntityID ResolvedEntityId{ EntityId };
        const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(ResolvedEntityId) };
        while (HierarchyComponent != nullptr && HierarchyComponent->parent != Arche::NullEntityID) {
            ResolvedEntityId = HierarchyComponent->parent;
            HierarchyComponent = World.GetComponent<Game::EntityHierarchy>(ResolvedEntityId);
        }

        return ResolvedEntityId;
    }

    Arche::EntityID ResolvePickedEntityForSelection(Arche::World& World, Arche::EntityID PickedEntityId, Arche::EntityID PreviouslyPickedEntityId) {
        if (PickedEntityId == Arche::NullEntityID) {
            return Arche::NullEntityID;
        }

        const Arche::EntityID TopLevelEntityId{ ResolveTopLevelEntityId(World, PickedEntityId) };
        if (TopLevelEntityId == Arche::NullEntityID) {
            return Arche::NullEntityID;
        }

        if (PreviouslyPickedEntityId == TopLevelEntityId && PickedEntityId != TopLevelEntityId) {
            return PickedEntityId;
        }

        return TopLevelEntityId;
    }

    Arche::EntityID ResolveHoveredGizmoEntity(const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& WorldMatrices, Arche::World& World, DirectX::FXMVECTOR RayOrigin, DirectX::FXMVECTOR RayDirection) {
        float NearestDistance{ std::numeric_limits<float>::max() };
        Arche::EntityID HoveredGizmoEntityId{ Arche::NullEntityID };

        for (const auto [MeshRenderer, HierarchyComponent, Gizmo] : World.Query<Game::StaticMeshRenderer, Game::EntityHierarchy, Game::PickingGizmo>()) {
            (void)Gizmo;
            if (MeshRenderer.active == false) {
                continue;
            }

            SimpleMath::Matrix GizmoWorldMatrix{ SimpleMath::Matrix::Identity };
            DirectX::BoundingOrientedBox GizmoWorldBounds{};
            if (TryResolveEntityWorldAndBounds(WorldMatrices, World, HierarchyComponent.self, GizmoWorldMatrix, GizmoWorldBounds) == false) {
                continue;
            }

            float HitDistance{ 0.0f };
            const bool IsIntersects{ GizmoWorldBounds.Intersects(RayOrigin, RayDirection, HitDistance) };
            if (IsIntersects && HitDistance < NearestDistance) {
                NearestDistance = HitDistance;
                HoveredGizmoEntityId = HierarchyComponent.self;
            }
        }

        return HoveredGizmoEntityId;
    }
}

namespace Game {
    const std::string& PickingSystem::Name() const {
        return mName;
    }

    Phase PickingSystem::GetPhase() const {
        return Phase::PostRender;
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

        if (Input.IsImGuiInputBlocked()) {
            if (mIsGizmoDragging) {
                mIsGizmoDragging = false;
                mDraggingTargetEntityId = Arche::NullEntityID;
            }

            const Arche::EntityID HoveredGizmoEntityId{ Arche::NullEntityID };
            if (mLastGizmoPickedEntityId != Ctx.PickedEntityId || mLastHoveredGizmoEntityId != HoveredGizmoEntityId) {
                UpdatePickingGizmos(Ctx.WorldMatrices, World, Ctx.PickedEntityId, HoveredGizmoEntityId);
                mLastGizmoPickedEntityId = Ctx.PickedEntityId;
                mLastHoveredGizmoEntityId = HoveredGizmoEntityId;
            }

            return;
        }

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
                    Arche::EntityID PickedEntityId{ ResolveHoveredGizmoEntity(Ctx.WorldMatrices, World, RayOrigin, RayDirection) };
                    if (PickedEntityId == Arche::NullEntityID) {
                        float NearestDistance{ std::numeric_limits<float>::max() };

                        for (const auto [TransformComponent, HierarchyComponent] : World.Query<Transform, EntityHierarchy>()) {
                            (void)TransformComponent;

                            if (HierarchyComponent.parent != Arche::NullEntityID) {
                                continue;
                            }

                            ResolvePickedEntityRecursive(Ctx.WorldMatrices, World, HierarchyComponent.self, RayOrigin, RayDirection, NearestDistance, PickedEntityId);
                        }
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
                            if (TryResolveEntityWorldMatrix(Ctx.WorldMatrices, DragTargetEntityId, TargetWorldMatrix)) {
                                const Transform* DragCameraTransform{ nullptr };
                                const Camera* DragCameraComponent{ nullptr };
                                DirectX::XMVECTOR DragRayOriginVector{};
                                DirectX::XMVECTOR DragRayDirectionVector{};
                                if (TryFindActiveCamera(World, DragCameraTransform, DragCameraComponent) && DragCameraTransform != nullptr && DragCameraComponent != nullptr && TryBuildPickingRay(*DragCameraTransform, *DragCameraComponent, DragRayOriginVector, DragRayDirectionVector)) {
                                    SimpleMath::Vector3 DragRayOrigin{};
                                    SimpleMath::Vector3 DragRayDirection{};
                                    DirectX::XMStoreFloat3(&DragRayOrigin, DragRayOriginVector);
                                    DirectX::XMStoreFloat3(&DragRayDirection, DragRayDirectionVector);

                                    const SimpleMath::Quaternion AxisOrientation{ SimpleMath::Quaternion::CreateFromRotationMatrix(TargetWorldMatrix) };
                                    const std::array<SimpleMath::Vector3, 3> AxisDirections{ { SimpleMath::Vector3::Transform(SimpleMath::Vector3::UnitX, AxisOrientation), SimpleMath::Vector3::Transform(SimpleMath::Vector3::UnitY, AxisOrientation), SimpleMath::Vector3::Transform(SimpleMath::Vector3::UnitZ, AxisOrientation) } };
                                    if (PickedGizmo->axisIndex < AxisDirections.size()) {
                                        const SimpleMath::Vector3 AxisDirection{ AxisDirections[PickedGizmo->axisIndex] };
                                        const SimpleMath::Vector3 AxisOrigin{ TargetWorldMatrix.Translation() };
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
                        const Arche::EntityID ResolvedEntityId{ ResolvePickedEntityForSelection(World, PickedEntityId, PreviouslyPickedEntityId) };
                        Ctx.PickedEntityId = ResolvedEntityId;

                        Game::PickedEntityChangedPayload PickedEntityChangedPayload{};
                        PickedEntityChangedPayload.PickedEntityId = ResolvedEntityId;
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
                        UpdatePickingGizmos(Ctx.WorldMatrices, World, Ctx.PickedEntityId, Arche::NullEntityID);
                    }
                }
            }
        }

        if (mIsGizmoDragging && IsMouseDragReleased) {
            mIsGizmoDragging = false;
            mDraggingTargetEntityId = Arche::NullEntityID;
        }

        DirectX::XMVECTOR HoverRayOrigin{};
        DirectX::XMVECTOR HoverRayDirection{};
        Arche::EntityID HoveredGizmoEntityId{ Arche::NullEntityID };

        const Transform* CameraTransform{ nullptr };
        const Camera* CameraComponent{ nullptr };
        const bool IsHoverRayAvailable{ mIsGizmoDragging == false && Ctx.PickedEntityId != Arche::NullEntityID && TryFindActiveCamera(World, CameraTransform, CameraComponent) && CameraTransform != nullptr && CameraComponent != nullptr && TryBuildPickingRay(*CameraTransform, *CameraComponent, HoverRayOrigin, HoverRayDirection) };
        if (IsHoverRayAvailable) {
            HoveredGizmoEntityId = ResolveHoveredGizmoEntity(Ctx.WorldMatrices, World, HoverRayOrigin, HoverRayDirection);
        }

        if (mLastGizmoPickedEntityId != Ctx.PickedEntityId || mLastHoveredGizmoEntityId != HoveredGizmoEntityId) {
            UpdatePickingGizmos(Ctx.WorldMatrices, World, Ctx.PickedEntityId, HoveredGizmoEntityId);
            mLastGizmoPickedEntityId = Ctx.PickedEntityId;
            mLastHoveredGizmoEntityId = HoveredGizmoEntityId;
        }
    }



}
