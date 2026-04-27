#include "StaticRenderSystem.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <utility>
#include "Game/Model/AssetRegistry.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/Culling.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Frustum.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    constexpr std::uint32_t PickedDrawFlagBitMask{ 0x1u };

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

    bool IsEntityWithinPickedHierarchy(Arche::World& World, Arche::EntityID EntityId, Arche::EntityID PickedEntityId) {
        if (PickedEntityId == Arche::NullEntityID) {
            return false;
        }

        Arche::EntityID CurrentEntityId{ EntityId };
        while (CurrentEntityId != Arche::NullEntityID) {
            if (CurrentEntityId == PickedEntityId) {
                return true;
            }

            const Game::EntityHierarchy* Hierarchy{ std::as_const(World).GetComponent<Game::EntityHierarchy>(CurrentEntityId) };
            if (Hierarchy == nullptr) {
                break;
            }

            CurrentEntityId = Hierarchy->parent;
        }

        return false;
    }

    const Game::RegisteredMaterialGroup* ResolveMaterialGroup(const std::vector<Game::RegisteredMaterialGroup>& MaterialGroups, const Game::Material* MaterialComponent) {
        if (MaterialGroups.empty() == true) {
            return nullptr;
        }

        std::size_t ResolvedMaterialGroupIndex{ MaterialComponent == nullptr ? 0u : MaterialComponent->MaterialGroupIndex };
        if (ResolvedMaterialGroupIndex >= MaterialGroups.size() || MaterialGroups[ResolvedMaterialGroupIndex].Items.empty()) {
            ResolvedMaterialGroupIndex = 0;
        }

        if (ResolvedMaterialGroupIndex >= MaterialGroups.size() || MaterialGroups[ResolvedMaterialGroupIndex].Items.empty() == true) {
            return nullptr;
        }

        return &MaterialGroups[ResolvedMaterialGroupIndex];
    }

    void AppendStaticDrawRecords(const std::vector<Game::ModelSubMesh>& SubMeshes, const Game::ModelNode& Node, const Game::RegisteredMaterialGroup* ResolvedMaterialGroup, std::uint32_t ObjectIndex, std::uint32_t MaterialFlags, std::uint32_t PickFlags, std::vector<Game::RFD::DrawRecord>& OutDrawRecords) {
        for (std::size_t SubMeshIndex{ 0 }; SubMeshIndex < SubMeshes.size(); ++SubMeshIndex) {
            const Game::ModelSubMesh& SubMesh{ SubMeshes[SubMeshIndex] };
            const Interface::IPipeline* Pipeline{ nullptr };
            std::uint32_t ResolvedMaterialIndex{ 0 };

            if (ResolvedMaterialGroup != nullptr) {
                std::size_t ResolvedItemIndex{ SubMesh.MaterialGroupItemIndex };
                if (ResolvedItemIndex >= ResolvedMaterialGroup->Items.size()) {
                    ResolvedItemIndex = 0;
                }

                if (ResolvedItemIndex < ResolvedMaterialGroup->Items.size()) {
                    const Game::RegisteredMaterialGroupItem& RegisteredGroupItem{ ResolvedMaterialGroup->Items[ResolvedItemIndex] };
                    Pipeline = RegisteredGroupItem.Pipeline;
                    ResolvedMaterialIndex = RegisteredGroupItem.MaterialIndex;
                }
            }

            Game::RFD::DrawRecord DrawRecord{};
            DrawRecord.pso = Pipeline;
            DrawRecord.mesh = &Node;
            DrawRecord.submesh = static_cast<std::uint32_t>(SubMeshIndex);
            DrawRecord.pass = 0;
            DrawRecord.objectIndex = ObjectIndex;
            DrawRecord.materialIndex = ResolvedMaterialIndex;
            DrawRecord.flags = MaterialFlags | PickFlags;
            DrawRecord.pad0 = 0;
            OutDrawRecords.push_back(DrawRecord);
        }
    }
}

namespace Game {
    const std::string& StaticRenderSystem::Name() const {
        return mName;
    }

    Phase StaticRenderSystem::GetPhase() const {
        return Phase::Render;
    }

    std::span<const ComponentAccess> StaticRenderSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 7> Accesses{ { { typeid(Transform), Access::Write }, { typeid(StaticMeshRenderer), Access::Read }, { typeid(EntityHierarchy), Access::Read }, { typeid(Material), Access::Read }, { typeid(BoundingBox), Access::Write }, { typeid(Frustum), Access::Read }, { typeid(Culling), Access::Read } } };
        return Accesses;
    }

    std::span<const ResourceAccess> StaticRenderSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 3> Accesses{ { { typeid(RFD::RenderFrameData), Access::Write }, { typeid(std::vector<RegisteredMaterialGroup>), Access::Read }, { typeid(Arche::EntityID), Access::Read } } };
        return Accesses;
    }

    void StaticRenderSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        UpdateWorldMatrices(World);

        RFD::RenderFrameData& RenderData{ Ctx.RenderData };
        const std::vector<RegisteredMaterialGroup>& MaterialGroups{ *Ctx.MaterialGroups };
        const Frustum* CullingFrustumComponent{ nullptr };
        const std::uint32_t ShadowCascadeCount{ RFD::ResolveShadowCascadeCount(RenderData.shadowMapping) };
        const std::array<DirectX::BoundingOrientedBox, RFD::ShadowCascadeMaxCount> ShadowCullingBoxes{ RFD::BuildShadowCullingBoxes(RenderData.shadowMapping) };

        for (auto [CameraComponent, FrustumComponent] : World.Query<Camera, Frustum>()) {
            if (CameraComponent.isActive == false) {
                continue;
            }

            CullingFrustumComponent = &FrustumComponent;
            break;
        }

        for (auto [TransformComponent, Renderer, HierarchyComponent] : World.Query<Transform, StaticMeshRenderer, EntityHierarchy>()) {
            const Arche::EntityID EntityId{ HierarchyComponent.self };
            const Material* MaterialComponent{ std::as_const(World).GetComponent<Material>(EntityId) };

            if (Renderer.model == nullptr || Renderer.active == false) {
                continue;
            }

            const SimpleMath::Matrix NodeWorld{ TransformComponent.worldMatrix };

            BoundingBox* BoundingBoxComponent{ World.GetComponent<BoundingBox>(EntityId) };
            if (BoundingBoxComponent != nullptr) {
                BoundingBoxComponent->UpdateWorldObb(NodeWorld);
            }

            const std::vector<ModelNode>& Nodes{ Renderer.model->GetNodes() };
            if (Renderer.nodeIndex >= Nodes.size()) {
                continue;
            }

            const ModelNode& Node{ Nodes[Renderer.nodeIndex] };
            const std::vector<ModelSubMesh>& SubMeshes{ Node.GetSubMeshes() };
            if (SubMeshes.empty()) {
                continue;
            }

            const std::uint32_t MaterialFlags{ MaterialComponent == nullptr ? 0u : MaterialComponent->Flags };
            const bool IsPickedHierarchy{ IsEntityWithinPickedHierarchy(World, EntityId, Ctx.PickedEntityId) };
            const std::uint32_t PickFlags{ IsPickedHierarchy ? PickedDrawFlagBitMask : 0u };
            const RegisteredMaterialGroup* ResolvedMaterialGroup{ ResolveMaterialGroup(MaterialGroups, MaterialComponent) };
            const bool IsVisible{ IsVisibleByFrustum(World, EntityId, CullingFrustumComponent) };

            if (IsVisible == true) {
                RFD::ModelContext ModelContext{};
                ModelContext.world = NodeWorld;
                ModelContext.prevWorld = ModelContext.world;

                if (BoundingBoxComponent != nullptr && BoundingBoxComponent->HasWorldObb() == true) {
                    const DirectX::BoundingOrientedBox& WorldObb{ BoundingBoxComponent->GetWorldObb() };
                    RFD::BoundingBoxContext BoundingBoxContext{};
                    BoundingBoxContext.center = SimpleMath::Vector4{ WorldObb.Center.x, WorldObb.Center.y, WorldObb.Center.z, 1.0f };
                    BoundingBoxContext.extents = SimpleMath::Vector4{ WorldObb.Extents.x, WorldObb.Extents.y, WorldObb.Extents.z, 0.0f };
                    BoundingBoxContext.orientation = SimpleMath::Vector4{ WorldObb.Orientation.x, WorldObb.Orientation.y, WorldObb.Orientation.z, WorldObb.Orientation.w };
                    RenderData.boundingBoxContexts.push_back(BoundingBoxContext);
                }

                ModelContext.objectID = static_cast<std::uint32_t>(RenderData.modelContexts.size());
                RenderData.modelContexts.push_back(ModelContext);
                AppendStaticDrawRecords(SubMeshes, Node, ResolvedMaterialGroup, ModelContext.objectID, MaterialFlags, PickFlags, RenderData.drawRecords);
            }

            for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1) {
                const bool IsVisibleByShadow{ IsVisibleByShadowBox(World, EntityId, ShadowCullingBoxes[CascadeIndex]) };
                if (IsVisibleByShadow == false) {
                    continue;
                }

                RFD::ShadowRenderContext& ShadowRenderContext{ RenderData.ShadowRenderContexts[CascadeIndex] };
                RFD::ModelContext ShadowModelContext{};
                ShadowModelContext.world = NodeWorld;
                ShadowModelContext.prevWorld = ShadowModelContext.world;
                ShadowModelContext.objectID = static_cast<std::uint32_t>(ShadowRenderContext.ModelContexts.size());
                ShadowRenderContext.ModelContexts.push_back(ShadowModelContext);
                AppendStaticDrawRecords(SubMeshes, Node, ResolvedMaterialGroup, ShadowModelContext.objectID, MaterialFlags, 0u, ShadowRenderContext.DrawRecords);
            }
        }
    }

    bool StaticRenderSystem::IsVisibleByFrustum(Arche::World& World, Arche::EntityID EntityId, const Frustum* CullingFrustumComponent) const {
        const Culling* CullingComponent{ std::as_const(World).GetComponent<Culling>(EntityId) };
        if (CullingComponent != nullptr && CullingComponent->frustumCulling == false) {
            return true;
        }

        if (CullingFrustumComponent == nullptr) {
            return true;
        }

        const BoundingBox* BoundingBoxComponent{ std::as_const(World).GetComponent<BoundingBox>(EntityId) };
        if (BoundingBoxComponent == nullptr || BoundingBoxComponent->HasWorldObb() == false) {
            return true;
        }

        return CullingFrustumComponent->Intersects(BoundingBoxComponent->GetWorldObb());
    }

    bool StaticRenderSystem::IsVisibleByShadowBox(Arche::World& World, Arche::EntityID EntityId, const DirectX::BoundingOrientedBox& CullingBox) const {
        const Culling* CullingComponent{ std::as_const(World).GetComponent<Culling>(EntityId) };
        if (CullingComponent != nullptr && CullingComponent->frustumCulling == false) {
            return true;
        }

        const BoundingBox* BoundingBoxComponent{ std::as_const(World).GetComponent<BoundingBox>(EntityId) };
        if (BoundingBoxComponent == nullptr || BoundingBoxComponent->HasWorldObb() == false) {
            return true;
        }

        return CullingBox.Intersects(BoundingBoxComponent->GetWorldObb());
    }
}
