#include "StaticRenderSystem.h"
#include <array>
#include <cstdint>
#include <vector>
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
            TraverseNode(*rootNode, nodes, entityWorld, renderData);
        }
    }

    void StaticRenderSystem::TraverseNode(const ModelNode& node, const std::vector<ModelNode>& nodes, const SimpleMath::Matrix& parentWorld, RFD::RenderFrameData& renderData) const {
        const SimpleMath::Matrix nodeWorld{ node.GetNodeToParent() * parentWorld };

        RFD::ModelContext context{};
        context.world = node.GetGeometryToNode() * nodeWorld;
        context.prevWorld = context.world;
        context.objectID = static_cast<std::uint32_t>(renderData.modelContexts.size());
        renderData.modelContexts.push_back(context);

        const std::vector<ModelSubMesh>& subMeshes{ node.GetSubMeshes() };
        for (std::size_t subMeshIndex{ 0 }; subMeshIndex < subMeshes.size(); ++subMeshIndex) {
            const ModelSubMesh& subMesh{ subMeshes[subMeshIndex] };

            RFD::DrawBatch batch{};
            batch.pso = nullptr;
            batch.mesh = &node;
            batch.submesh = static_cast<std::uint32_t>(subMeshIndex);
            batch.pass = 0;

            renderData.batches.push_back(batch);
            renderData.drawInstances.emplace_back(context.objectID, static_cast<std::uint32_t>(subMesh.MaterialIndex));
        }

        for (std::uint32_t childIndex : node.GetChildren()) {
            if (childIndex >= nodes.size()) {
                continue;
            }

            TraverseNode(nodes[childIndex], nodes, nodeWorld, renderData);
        }
    }
}
