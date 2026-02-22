#include "StaticRenderSystem.h"
#include <array>
#include <cstdint>
#include <vector>
#include "Game/Model/AssetRegistry.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Components/Material.h"

namespace {
    SimpleMath::Matrix BuildWorldMatrix(const Game::Transform& transform) {
        return SimpleMath::Matrix::CreateScale(transform.scale)
            * SimpleMath::Matrix::CreateFromQuaternion(transform.rotation)
            * SimpleMath::Matrix::CreateTranslation(transform.position);
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
        static std::array<ComponentAccess, 3> accesses{ {
            {typeid(Transform), Access::Read},
            {typeid(StaticMeshRenderer), Access::Read},
			{typeid(Material), Access::Read}
        } };
        return accesses;
    }

    std::span<const ResourceAccess> StaticRenderSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 1> accesses{ {
            {typeid(RFD::RenderFrameData), Access::Write}
        } };
        return accesses;
    }

    void StaticRenderSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        RFD::RenderFrameData& renderData{ Ctx.RenderData };
        const std::vector<RegisteredMaterialGroup>* materialGroups{ Ctx.MaterialGroups };

        // submesh 마다 다른 pso 를 사용하는 방법은..? 
        for (auto [renderer, transform, material] : World.Query<StaticMeshRenderer, Transform, Material>()) {
            if (renderer.modelNode == nullptr) {
                continue;
            }

            const Model* model{ renderer.modelNode };
            const ModelNode* rootNode{ model->GetRootNode() };
            if (rootNode == nullptr) {
                continue;
            }

            const std::vector<ModelNode>& nodes{ model->GetNodes() };
            const SimpleMath::Matrix entityWorld{ BuildWorldMatrix(transform) };
            TraverseNode(*rootNode, nodes, entityWorld, material.MaterialGroupIndex, Ctx, renderData);
        }
    }

    void StaticRenderSystem::TraverseNode(const ModelNode& Node, const std::vector<ModelNode>& Nodes, const SimpleMath::Matrix& ParentWorld, std::uint32_t MaterialGroupIndex, FrameContext& Context, RFD::RenderFrameData& RenderData) const {
        const SimpleMath::Matrix NodeWorld{ Node.GetNodeToParent() * ParentWorld };

        RFD::ModelContext ModelContext{};
        ModelContext.world = Node.GetGeometryToNode() * NodeWorld;
        ModelContext.prevWorld = ModelContext.world;
        ModelContext.objectID = static_cast<std::uint32_t>(RenderData.modelContexts.size());
        RenderData.modelContexts.push_back(ModelContext);

        const std::vector<ModelSubMesh>& SubMeshes{ Node.GetSubMeshes() };
        for (std::size_t SubMeshIndex{ 0 }; SubMeshIndex < SubMeshes.size(); ++SubMeshIndex) {
            const ModelSubMesh& SubMesh{ SubMeshes[SubMeshIndex] };

            RFD::DrawBatch Batch{};
            Batch.pso = nullptr;
            Batch.mesh = &Node;
            Batch.submesh = static_cast<std::uint32_t>(SubMeshIndex);
            Batch.pass = 0;

            std::uint32_t ResolvedMaterialIndex{ 0 };
            if (Context.MaterialGroups != nullptr && MaterialGroupIndex < Context.MaterialGroups->size()) {
                const RegisteredMaterialGroup& RegisteredGroup{ (*Context.MaterialGroups)[MaterialGroupIndex] };
                if (SubMesh.MaterialGroupItemIndex < RegisteredGroup.Items.size()) {
                    const RegisteredMaterialGroupItem& RegisteredGroupItem{ RegisteredGroup.Items[SubMesh.MaterialGroupItemIndex] };
                    Batch.pso = RegisteredGroupItem.Pipeline;
                    ResolvedMaterialIndex = RegisteredGroupItem.MaterialIndex;
                }
            }

        
            RenderData.batches.push_back(Batch);
            RenderData.drawInstances.emplace_back(ModelContext.objectID, ResolvedMaterialIndex);
        }


        for (std::uint32_t ChildIndex : Node.GetChildren()) {
            if (ChildIndex >= Nodes.size()) {
                continue;
            }
            TraverseNode(Nodes[ChildIndex], Nodes, NodeWorld, MaterialGroupIndex, Context, RenderData);
        }
            
    }
}
