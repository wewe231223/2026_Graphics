#include "StaticRenderSystem.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "Game/Model/AssetRegistry.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    SimpleMath::Matrix BuildLocalWorldMatrix(const Game::Transform& TransformComponent) {
        const SimpleMath::Matrix TrsMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
        return TransformComponent.nodeToParent * TrsMatrix;
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
        static std::array<ComponentAccess, 3> Accesses{ { { typeid(Transform), Access::Read }, { typeid(StaticMeshRenderer), Access::Read }, { typeid(EntityHierarchy), Access::Read } } };
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
            if (Renderer.model == nullptr) {
                continue;
            }

            if (Hierarchy.parent != Arche::NullEntityID) {
                continue;
            }

            const SimpleMath::Matrix LocalWorld{ BuildLocalWorldMatrix(TransformComponent) };
            TraverseHierarchy(World, Hierarchy.self, LocalWorld, RenderData, MaterialGroups);
        }
    }

    void StaticRenderSystem::TraverseHierarchy(Arche::World& World, Arche::EntityID EntityId, const SimpleMath::Matrix& ParentWorld, RFD::RenderFrameData& RenderData, const std::vector<RegisteredMaterialGroup>& MaterialGroups) const {
        const StaticMeshRenderer* Renderer{ World.GetComponent<StaticMeshRenderer>(EntityId) };
        const Transform* TransformComponent{ World.GetComponent<Transform>(EntityId) };
        const EntityHierarchy* Hierarchy{ World.GetComponent<EntityHierarchy>(EntityId) };

        if (Renderer == nullptr || TransformComponent == nullptr || Hierarchy == nullptr || Renderer->model == nullptr) {
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
                if (MaterialGroups.empty() == false && Renderer->materialGroupIndex < MaterialGroups.size()) {
                    const auto& RegisteredGroup{ MaterialGroups[Renderer->materialGroupIndex] };
                    if (SubMesh.MaterialGroupItemIndex < RegisteredGroup.Items.size()) {
                        const RegisteredMaterialGroupItem& RegisteredGroupItem{ RegisteredGroup.Items[SubMesh.MaterialGroupItemIndex] };
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
                DrawRecord.flags = 0;
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
            TraverseHierarchy(World, ChildId, ChildWorld, RenderData, MaterialGroups);
            ChildId = ChildHierarchy->nextSibling;
        }
    }
}
