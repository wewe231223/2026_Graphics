#include "StaticRenderSystem.h"
#include <array>
#include <cstdint>
#include <vector>
#include "Game/Model/AssetRegistry.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    SimpleMath::Matrix BuildWorldMatrix(const Game::Transform& Transform) {
        return SimpleMath::Matrix::CreateScale(Transform.scale) * SimpleMath::Matrix::CreateFromQuaternion(Transform.rotation) * SimpleMath::Matrix::CreateTranslation(Transform.position);
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
        static std::array<ComponentAccess, 3> accesses{ { { typeid(Transform), Access::Read }, { typeid(StaticMeshRenderer), Access::Read }, { typeid(Material), Access::Read } } };
        return accesses;
    }

    std::span<const ResourceAccess> StaticRenderSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 1> accesses{ { { typeid(RFD::RenderFrameData), Access::Write } } };
        return accesses;
    }

    void StaticRenderSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        RFD::RenderFrameData& RenderData{ Ctx.RenderData };
        const std::vector<RegisteredMaterialGroup>& MaterialGroups{ *Ctx.MaterialGroups };

        for (auto [Renderer, Transform, Material] : World.Query<StaticMeshRenderer, Transform, Material>()) {
            if (Renderer.modelNode == nullptr) {
                continue;
            }

            const Model* ModelData{ Renderer.modelNode };
            const ModelNode* RootNode{ ModelData->GetRootNode() };
            if (RootNode == nullptr) {
                continue;
            }

            const std::vector<ModelNode>& Nodes{ ModelData->GetNodes() };
            const SimpleMath::Matrix EntityWorld{ BuildWorldMatrix(Transform) };
            TraverseNode(*RootNode, Nodes, EntityWorld, Material.MaterialGroupIndex, MaterialGroups, RenderData);
        }
    }

    void StaticRenderSystem::TraverseNode(const ModelNode& Node, const std::vector<ModelNode>& Nodes, const SimpleMath::Matrix& ParentWorld, std::uint32_t MaterialGroupIndex, const std::vector<RegisteredMaterialGroup>& MaterialGroups, RFD::RenderFrameData& RenderData) const {
        const SimpleMath::Matrix NodeWorld{ Node.GetNodeToParent() * ParentWorld };

        RFD::ModelContext ModelContext{};
        ModelContext.world = Node.GetGeometryToNode() * NodeWorld;
        ModelContext.prevWorld = ModelContext.world;
        ModelContext.objectID = static_cast<std::uint32_t>(RenderData.modelContexts.size());
        RenderData.modelContexts.push_back(ModelContext);

        const std::vector<ModelSubMesh>& SubMeshes{ Node.GetSubMeshes() };
        for (std::size_t SubMeshIndex{ 0 }; SubMeshIndex < SubMeshes.size(); ++SubMeshIndex) {
            const ModelSubMesh& SubMesh{ SubMeshes[SubMeshIndex] };

            const Interface::IPipeline* Pipeline{ nullptr };
            std::uint32_t ResolvedMaterialIndex{ 0 };
            if (!MaterialGroups.empty() && MaterialGroupIndex < MaterialGroups.size()) {
                const RegisteredMaterialGroup& RegisteredGroup{ MaterialGroups[MaterialGroupIndex] };
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

        for (std::uint32_t ChildIndex : Node.GetChildren()) {
            if (ChildIndex >= Nodes.size()) {
                continue;
            }

            TraverseNode(Nodes[ChildIndex], Nodes, NodeWorld, MaterialGroupIndex, MaterialGroups, RenderData);
        }
    }
}
