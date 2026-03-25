#include "PickingSystem.h"

#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>

#include "Core/Config.h"
#include "Game/Base/Input.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/PickingGizmo.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"

#ifdef max
#undef max
#endif

namespace {
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

    bool TryResolveParentWorldMatrix(const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& WorldMatrices, Arche::World& World, Arche::EntityID EntityId, SimpleMath::Matrix& OutParentWorldMatrix) {
        const Game::EntityHierarchy* HierarchyComponent{ World.GetComponent<Game::EntityHierarchy>(EntityId) };
        if (HierarchyComponent == nullptr || HierarchyComponent->parent == Arche::NullEntityID) {
            OutParentWorldMatrix = SimpleMath::Matrix::Identity;
            return true;
        }

        return TryResolveEntityWorldMatrix(WorldMatrices, HierarchyComponent->parent, OutParentWorldMatrix);
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

    bool TryResolveGizmoAxisDirection(std::uint32_t AxisIndex, SimpleMath::Vector3& OutAxisDirection) {
        if (AxisIndex == 0u) {
            OutAxisDirection = SimpleMath::Vector3::UnitX;
            return true;
        }

        if (AxisIndex == 1u) {
            OutAxisDirection = SimpleMath::Vector3::UnitY;
            return true;
        }

        if (AxisIndex == 2u) {
            OutAxisDirection = SimpleMath::Vector3::UnitZ;
            return true;
        }

        return false;
    }

    void UpdatePickingGizmos(const std::unordered_map<Arche::EntityID, SimpleMath::Matrix>& WorldMatrices, Arche::World& World, Arche::EntityID PickedEntityId, Arche::EntityID HoveredGizmoEntityId) {
        DeactivatePickingGizmos(World);
        if (PickedEntityId == Arche::NullEntityID) {
            return;
        }

        const Game::Transform* SelectedTransformComponent{ World.GetComponent<Game::Transform>(PickedEntityId) };
        if (SelectedTransformComponent == nullptr) {
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

            SimpleMath::Matrix SelectedWorldMatrix{};
            if (TryResolveEntityWorldMatrix(WorldMatrices, PickedEntityId, SelectedWorldMatrix)) {
                AxisOrientation = SimpleMath::Quaternion::CreateFromRotationMatrix(SelectedWorldMatrix);
            }
            else {
                AxisOrientation = SelectedTransformComponent->rotation;
            }
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
        const DirectX::Mouse::ButtonStateTracker& MouseTracker{ Input.GetMouseTracker() };
        const bool IsMousePressed{ MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::PRESSED };
        const bool IsMouseHeld{ MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::HELD };
        const bool IsMouseReleased{ MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::RELEASED || MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::UP };

        DirectX::XMVECTOR RayOriginVector{};
        DirectX::XMVECTOR RayDirectionVector{};
        Arche::EntityID HoveredGizmoEntityId{ Arche::NullEntityID };

        const Transform* CameraTransform{ nullptr };
        const Camera* CameraComponent{ nullptr };
        const bool IsRayAvailable{ Ctx.PickedEntityId != Arche::NullEntityID && TryFindActiveCamera(World, CameraTransform, CameraComponent) && CameraTransform != nullptr && CameraComponent != nullptr && TryBuildPickingRay(*CameraTransform, *CameraComponent, RayOriginVector, RayDirectionVector) };
        if (IsRayAvailable) {
            HoveredGizmoEntityId = ResolveHoveredGizmoEntity(Ctx.WorldMatrices, World, RayOriginVector, RayDirectionVector);
        }

        if (IsMousePressed && HoveredGizmoEntityId != Arche::NullEntityID && Ctx.PickedEntityId != Arche::NullEntityID) {
            const PickingGizmo* HoveredGizmoComponent{ World.GetComponent<PickingGizmo>(HoveredGizmoEntityId) };
            const Transform* TargetTransform{ World.GetComponent<Transform>(Ctx.PickedEntityId) };
            SimpleMath::Matrix TargetWorldMatrix{};
            SimpleMath::Matrix ParentWorldMatrix{};
            if (HoveredGizmoComponent != nullptr && TargetTransform != nullptr && TryResolveEntityWorldMatrix(Ctx.WorldMatrices, Ctx.PickedEntityId, TargetWorldMatrix) && TryResolveParentWorldMatrix(Ctx.WorldMatrices, World, Ctx.PickedEntityId, ParentWorldMatrix)) {
                SimpleMath::Vector3 RayOrigin{};
                SimpleMath::Vector3 RayDirection{};
                DirectX::XMStoreFloat3(&RayOrigin, RayOriginVector);
                DirectX::XMStoreFloat3(&RayDirection, RayDirectionVector);

                const SimpleMath::Quaternion AxisOrientation{ SimpleMath::Quaternion::CreateFromRotationMatrix(TargetWorldMatrix) };
                const std::array<SimpleMath::Vector3, 3> AxisDirections{ { SimpleMath::Vector3::Transform(SimpleMath::Vector3::UnitX, AxisOrientation), SimpleMath::Vector3::Transform(SimpleMath::Vector3::UnitY, AxisOrientation), SimpleMath::Vector3::Transform(SimpleMath::Vector3::UnitZ, AxisOrientation) } };
                if (HoveredGizmoComponent->axisIndex < AxisDirections.size()) {
                    const SimpleMath::Vector3 AxisDirection{ AxisDirections[HoveredGizmoComponent->axisIndex] };
                    const SimpleMath::Vector3 AxisOrigin{ TargetWorldMatrix.Translation() };
                    float AxisParameter{};
                    if (TryResolveAxisRayParameter(RayOrigin, RayDirection, AxisOrigin, AxisDirection, AxisParameter)) {
                        SimpleMath::Vector3 AxisDirectionLocal{};
                        if (TryResolveGizmoAxisDirection(HoveredGizmoComponent->axisIndex, AxisDirectionLocal)) {
                            SimpleMath::Vector3 AxisDirectionParentSpace{ SimpleMath::Vector3::TransformNormal(AxisDirectionLocal, SimpleMath::Matrix::CreateFromQuaternion(TargetTransform->rotation)) };
                            if (AxisDirectionParentSpace.LengthSquared() > 0.0f) {
                                AxisDirectionParentSpace.Normalize();

                                SimpleMath::Matrix ParentWorldInverseMatrix{ ParentWorldMatrix };
                                ParentWorldInverseMatrix = ParentWorldInverseMatrix.Invert();

                                const SimpleMath::Vector3 RayOriginParentSpace{ SimpleMath::Vector3::Transform(RayOrigin, ParentWorldInverseMatrix) };
                                const SimpleMath::Vector3 RayDirectionParentSpace{ SimpleMath::Vector3::TransformNormal(RayDirection, ParentWorldInverseMatrix) };
                                float ParentAxisParameter{};
                                if (TryResolveAxisRayParameter(RayOriginParentSpace, RayDirectionParentSpace, TargetTransform->position, AxisDirectionParentSpace, ParentAxisParameter)) {
                                    mIsGizmoDragging = true;
                                    mDraggingTargetEntityId = Ctx.PickedEntityId;
                                    mDraggingTargetStartLocalPosition = TargetTransform->position;
                                    mDraggingAxisOriginParentSpace = TargetTransform->position;
                                    mDraggingAxisDirectionParentSpace = AxisDirectionParentSpace;
                                    mDraggingParentWorldInverseMatrix = ParentWorldInverseMatrix;
                                    mDraggingStartAxisParameter = ParentAxisParameter;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (mIsGizmoDragging && IsMouseHeld && mDraggingTargetEntityId != Arche::NullEntityID) {
            if (IsRayAvailable) {
                Game::Transform* TargetTransform{ World.GetComponent<Transform>(mDraggingTargetEntityId) };
                if (TargetTransform != nullptr) {
                    SimpleMath::Vector3 RayOrigin{};
                    SimpleMath::Vector3 RayDirection{};
                    DirectX::XMStoreFloat3(&RayOrigin, RayOriginVector);
                    DirectX::XMStoreFloat3(&RayDirection, RayDirectionVector);

                    const SimpleMath::Vector3 RayOriginParentSpace{ SimpleMath::Vector3::Transform(RayOrigin, mDraggingParentWorldInverseMatrix) };
                    const SimpleMath::Vector3 RayDirectionParentSpace{ SimpleMath::Vector3::TransformNormal(RayDirection, mDraggingParentWorldInverseMatrix) };
                    float CurrentAxisParameter{};
                    if (TryResolveAxisRayParameter(RayOriginParentSpace, RayDirectionParentSpace, mDraggingAxisOriginParentSpace, mDraggingAxisDirectionParentSpace, CurrentAxisParameter)) {
                        const float DeltaAxisParameter{ mDraggingStartAxisParameter - CurrentAxisParameter };
                        TargetTransform->position = mDraggingTargetStartLocalPosition + mDraggingAxisDirectionParentSpace * DeltaAxisParameter;
                    }
                }
            }
        }

        if (mIsGizmoDragging && IsMouseReleased) {
            mIsGizmoDragging = false;
            mDraggingTargetEntityId = Arche::NullEntityID;
            mDraggingStartAxisParameter = 0.0f;
            mDraggingTargetStartLocalPosition = SimpleMath::Vector3{};
            mDraggingAxisOriginParentSpace = SimpleMath::Vector3{};
            mDraggingAxisDirectionParentSpace = SimpleMath::Vector3{};
            mDraggingParentWorldInverseMatrix = SimpleMath::Matrix::Identity;
        }

        if (mIsGizmoDragging) {
            HoveredGizmoEntityId = Arche::NullEntityID;
        }

        UpdatePickingGizmos(Ctx.WorldMatrices, World, Ctx.PickedEntityId, HoveredGizmoEntityId);
    }
}
