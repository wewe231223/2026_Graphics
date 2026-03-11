#include "StaticRenderSystem.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "Game/Model/AssetRegistry.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    SimpleMath::Matrix BuildLocalWorldMatrix(const Game::Transform& TransformComponent) {
        const SimpleMath::Matrix TrsMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
        return TransformComponent.nodeToParent * TrsMatrix;
    }
}

namespace {
    constexpr std::uint32_t PickedDrawFlagBitMask{ 0x1u };
}

namespace Game {
    const std::string& StaticRenderSystem::Name() const {
        return mName;
    }

    Phase StaticRenderSystem::GetPhase() const {
        return Phase::Render;
    }

    std::span<const ComponentAccess> StaticRenderSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 4> Accesses{ { { typeid(Transform), Access::Read }, { typeid(StaticMeshRenderer), Access::Read }, { typeid(EntityHierarchy), Access::Read }, { typeid(Material), Access::Read } } };
        return Accesses;
    }

    std::span<const ResourceAccess> StaticRenderSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 2> Accesses{ { { typeid(RFD::RenderFrameData), Access::Write }, { typeid(std::vector<RegisteredMaterialGroup>), Access::Read } } };
        return Accesses;
    }

    void StaticRenderSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        RFD::RenderFrameData& RenderData{ Ctx.RenderData };
        const std::vector<RegisteredMaterialGroup>& MaterialGroups{ *Ctx.MaterialGroups };

        for (auto [Renderer, TransformComponent, Hierarchy] : World.Query<StaticMeshRenderer, Transform, EntityHierarchy>()) {
            if (Renderer.model == nullptr || Renderer.active == false) {
                continue;
            }

            if (Hierarchy.parent != Arche::NullEntityID) {
                continue;
            }

            const SimpleMath::Matrix LocalWorld{ BuildLocalWorldMatrix(TransformComponent) };
            TraverseHierarchy(World, Hierarchy.self, LocalWorld, RenderData, MaterialGroups, Ctx.PickedEntityId);
        }
    }

    void StaticRenderSystem::TraverseHierarchy(Arche::World& World, Arche::EntityID EntityId, const SimpleMath::Matrix& ParentWorld, RFD::RenderFrameData& RenderData, const std::vector<RegisteredMaterialGroup>& MaterialGroups, Arche::EntityID PickedEntityId) const {
        const StaticMeshRenderer* Renderer{ World.GetComponent<StaticMeshRenderer>(EntityId) };
        const Transform* TransformComponent{ World.GetComponent<Transform>(EntityId) };
        const EntityHierarchy* Hierarchy{ World.GetComponent<EntityHierarchy>(EntityId) };
        const Material* MaterialComponent{ World.GetComponent<Material>(EntityId) };

        if (Renderer == nullptr || TransformComponent == nullptr || Hierarchy == nullptr || Renderer->model == nullptr || Renderer->active == false) {
            return;
        }

        const std::vector<ModelNode>& Nodes{ Renderer->model->GetNodes() };
        if (Renderer->nodeIndex >= Nodes.size()) {
            return;
        }

        const ModelNode& Node{ Nodes[Renderer->nodeIndex] };
        const SimpleMath::Matrix NodeWorld{ ParentWorld };
        const std::vector<ModelSubMesh>& SubMeshes{ Node.GetSubMeshes() };

        if (SubMeshes.empty() == false) {
            RFD::ModelContext ModelContext{};
            ModelContext.world = TransformComponent->geometryToNode * NodeWorld;
            ModelContext.prevWorld = ModelContext.world;
            ModelContext.objectID = static_cast<std::uint32_t>(RenderData.modelContexts.size());
            RenderData.modelContexts.push_back(ModelContext);

            for (std::size_t SubMeshIndex{ 0 }; SubMeshIndex < SubMeshes.size(); ++SubMeshIndex) {
                const ModelSubMesh& SubMesh{ SubMeshes[SubMeshIndex] };

                const Interface::IPipeline* Pipeline{ nullptr };
                std::uint32_t ResolvedMaterialIndex{ 0 };
                std::uint32_t ResolvedMaterialGroupIndex{ Renderer->materialGroupIndex };
                if (MaterialGroups.empty() == false && (ResolvedMaterialGroupIndex >= MaterialGroups.size() || MaterialGroups[ResolvedMaterialGroupIndex].Items.empty())) {
                    ResolvedMaterialGroupIndex = 0;
                }

                if (MaterialGroups.empty() == false && ResolvedMaterialGroupIndex < MaterialGroups.size()) {
                    const RegisteredMaterialGroup& RegisteredGroup{ MaterialGroups[ResolvedMaterialGroupIndex] };
                    std::size_t ResolvedItemIndex{ SubMesh.MaterialGroupItemIndex };
                    if (ResolvedItemIndex >= RegisteredGroup.Items.size()) {
                        ResolvedItemIndex = 0;
                    }

                    if (ResolvedItemIndex < RegisteredGroup.Items.size()) {
                        const RegisteredMaterialGroupItem& RegisteredGroupItem{ RegisteredGroup.Items[ResolvedItemIndex] };
                        Pipeline = RegisteredGroupItem.Pipeline;
                        ResolvedMaterialIndex = RegisteredGroupItem.MaterialIndex;
                    }
                }

                RFD::DrawRecord DrawRecord{};
                DrawRecord.pso = Pipeline;
                DrawRecord.mesh = &Node;
                DrawRecord.submesh = static_cast<std::uint32_t>(SubMeshIndex);
                DrawRecord.pass = 0;
                DrawRecord.objectIndex = ModelContext.objectID;
                DrawRecord.materialIndex = ResolvedMaterialIndex;
                const std::uint32_t MaterialFlags{ MaterialComponent == nullptr ? 0u : MaterialComponent->Flags };
                const std::uint32_t PickFlags{ EntityId == PickedEntityId ? PickedDrawFlagBitMask : 0u };
                DrawRecord.flags = MaterialFlags | PickFlags;
                DrawRecord.pad0 = 0;
                RenderData.drawRecords.push_back(DrawRecord);
            }
        }

        Arche::EntityID ChildId{ Hierarchy->firstChild };
        while (ChildId != Arche::NullEntityID) {
            const Transform* ChildTransform{ World.GetComponent<Transform>(ChildId) };
            const EntityHierarchy* ChildHierarchy{ World.GetComponent<EntityHierarchy>(ChildId) };

            if (ChildTransform == nullptr || ChildHierarchy == nullptr) {
                break;
            }

            const SimpleMath::Matrix ChildWorld{ BuildLocalWorldMatrix(*ChildTransform) * NodeWorld };
            TraverseHierarchy(World, ChildId, ChildWorld, RenderData, MaterialGroups, PickedEntityId);
            ChildId = ChildHierarchy->nextSibling;
        }
    }
}
