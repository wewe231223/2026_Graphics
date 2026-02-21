#include "StaticRenderSystem.h"
#include <array>
#include <cstdint>
#include <iterator>
#include <vector>
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"

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
        static std::array<ComponentAccess, 2> accesses{ {
           {typeid(Transform), Access::Read},
            {typeid(StaticMeshRenderer), Access::Read}
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

        InstanceBucketMap drawInstanceBuckets{};
        std::vector<DrawBatchKey> batchOrder{};

        for (auto [renderer, transform] : World.Query<StaticMeshRenderer, Transform>()) {
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
            TraverseNode(*rootNode, nodes, entityWorld, renderData, drawInstanceBuckets, batchOrder);
        }

        for (const DrawBatchKey& key : batchOrder) {
            const auto foundBucket{ drawInstanceBuckets.find(key) };
            if (foundBucket == drawInstanceBuckets.end()) {
                continue;
            }

            std::vector<RFD::DrawInstance>& bucket{ foundBucket->second };
            if (bucket.empty()) {
                continue;
            }

            RFD::DrawBatch batch{};
            batch.pso = key.pso;
            batch.mesh = key.mesh;
            batch.submesh = key.submesh;
            batch.pass = key.pass;
            batch.instanceOffset = static_cast<std::uint32_t>(renderData.drawInstances.size());
            batch.instanceCount = static_cast<std::uint32_t>(bucket.size());

            renderData.drawInstances.insert(
                renderData.drawInstances.end(),
                std::make_move_iterator(bucket.begin()),
                std::make_move_iterator(bucket.end()));

            renderData.batches.push_back(batch);
        }
    }

    void StaticRenderSystem::TraverseNode(const ModelNode& node, const std::vector<ModelNode>& nodes, const SimpleMath::Matrix& parentWorld, RFD::RenderFrameData& renderData, InstanceBucketMap& drawInstanceBuckets, std::vector<DrawBatchKey>& batchOrder) const { const SimpleMath::Matrix nodeWorld{ node.GetNodeToParent() * parentWorld };

        RFD::ModelContext context{};
        context.world = node.GetGeometryToNode() * nodeWorld;
        context.prevWorld = context.world;
        context.objectID = static_cast<std::uint32_t>(renderData.modelContexts.size());
        const std::uint32_t objectIndex{ context.objectID };
        renderData.modelContexts.push_back(context);

        const std::vector<ModelSubMesh>& subMeshes{ node.GetSubMeshes() };
        for (std::size_t subMeshIndex{ 0 }; subMeshIndex < subMeshes.size(); ++subMeshIndex) {
            const ModelSubMesh& subMesh{ subMeshes[subMeshIndex] };

            DrawBatchKey key{};
            key.pso = nullptr;
            key.mesh = &node;
            key.submesh = static_cast<std::uint32_t>(subMeshIndex);
            key.pass = 0;

            auto [bucketIt, inserted] = drawInstanceBuckets.try_emplace(key, InstanceBucket{});
            if (inserted) {
                batchOrder.push_back(key);
            }

            RFD::DrawInstance instance{};
            instance.objectIndex = objectIndex;
            instance.materialIndex = static_cast<std::uint32_t>(subMesh.MaterialIndex);
            bucketIt->second.push_back(instance);
        }

        for (std::uint32_t childIndex : node.GetChildren()) {
            if (childIndex >= nodes.size()) {
                continue;
            }

            TraverseNode(nodes[childIndex], nodes, nodeWorld, renderData, drawInstanceBuckets, batchOrder);
        }
    }
}
