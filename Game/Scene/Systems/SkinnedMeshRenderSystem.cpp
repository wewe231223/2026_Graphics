#include "SkinnedMeshRenderSystem.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <utility>
#include "Game/Model/AssetRegistry.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/SkinnedMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"

namespace {
    constexpr std::uint32_t SkinnedModelContextFlagBitMask{ 0x1u };
    constexpr std::uint32_t PickedDrawFlagBitMask{ 0x1u };

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
}

namespace Game {
    SkinnedMeshRenderSystem::SkinnedMeshRenderSystem() {
    }

    SkinnedMeshRenderSystem::~SkinnedMeshRenderSystem() {
    }

    const std::string& SkinnedMeshRenderSystem::Name() const {
        return mName;
    }

    Phase SkinnedMeshRenderSystem::GetPhase() const {
        return Phase::Render;
    }

    std::span<const ComponentAccess> SkinnedMeshRenderSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 4> Accesses{ { { typeid(SkinnedMeshRenderer), Access::Read }, { typeid(Transform), Access::Read }, { typeid(EntityHierarchy), Access::Read }, { typeid(BoundingBox), Access::Read } } };
        return Accesses;
    }

    std::span<const ResourceAccess> SkinnedMeshRenderSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 4> Accesses{ { { typeid(RFD::RenderFrameData), Access::Write }, { typeid(std::vector<RegisteredMaterialGroup>), Access::Read }, { typeid(Arche::EntityID), Access::Read }, { typeid(std::vector<SkinnedMeshPreparedData>), Access::Read } } };
        return Accesses;
    }

    void SkinnedMeshRenderSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        RFD::RenderFrameData& RenderData{ Ctx.RenderData };
        const std::vector<RegisteredMaterialGroup>& MaterialGroups{ *Ctx.MaterialGroups };
        std::unordered_map<Arche::EntityID, const SkinnedMeshPreparedData*> PreparedDataByEntity{};
        PreparedDataByEntity.reserve(Ctx.SkinnedMeshPreparedDataItems.size());
        for (const SkinnedMeshPreparedData& PreparedData : Ctx.SkinnedMeshPreparedDataItems) {
            PreparedDataByEntity[PreparedData.EntityId] = &PreparedData;
        }

        for (auto [TransformComponent, Renderer, HierarchyComponent] : World.Query<Transform, SkinnedMeshRenderer, EntityHierarchy>()) {
            if (Renderer.model == nullptr || Renderer.active == false) {
                continue;
            }

            const Arche::EntityID EntityId{ HierarchyComponent.self };
            const std::unordered_map<Arche::EntityID, const SkinnedMeshPreparedData*>::const_iterator PreparedDataIter{ PreparedDataByEntity.find(EntityId) };
            if (PreparedDataIter == PreparedDataByEntity.end()) {
                continue;
            }

            const SkinnedMeshPreparedData& PreparedData{ *PreparedDataIter->second };
            const std::vector<ModelNode>& Nodes{ Renderer.model->GetNodes() };
            if (Renderer.nodeIndex >= Nodes.size()) {
                continue;
            }

            const ModelNode& Node{ Nodes[Renderer.nodeIndex] };
            const std::vector<ModelSubMesh>& SubMeshes{ Node.GetSubMeshes() };
            if (SubMeshes.empty() == true || PreparedData.BonePalette.empty() == true) {
                continue;
            }

            const std::uint32_t GlobalBoneIndexStart{ static_cast<std::uint32_t>(RenderData.bonePalette.size()) };
            RenderData.bonePalette.insert(RenderData.bonePalette.end(), PreparedData.BonePalette.begin(), PreparedData.BonePalette.end());
            RenderData.boundingBoxContexts.insert(RenderData.boundingBoxContexts.end(), PreparedData.BoneBoundingBoxContexts.begin(), PreparedData.BoneBoundingBoxContexts.end());

            BoundingBox* BoundingBoxComponent{ World.GetComponent<BoundingBox>(EntityId) };
            if (BoundingBoxComponent != nullptr && BoundingBoxComponent->HasWorldObb() == true) {
                const DirectX::BoundingOrientedBox& WorldObb{ BoundingBoxComponent->GetWorldObb() };
                RFD::BoundingBoxContext BoundingBoxContext{};
                BoundingBoxContext.center = SimpleMath::Vector4{ WorldObb.Center.x, WorldObb.Center.y, WorldObb.Center.z, 1.0f };
                BoundingBoxContext.extents = SimpleMath::Vector4{ WorldObb.Extents.x, WorldObb.Extents.y, WorldObb.Extents.z, 0.0f };
                BoundingBoxContext.orientation = SimpleMath::Vector4{ WorldObb.Orientation.x, WorldObb.Orientation.y, WorldObb.Orientation.z, WorldObb.Orientation.w };
                RenderData.boundingBoxContexts.push_back(BoundingBoxContext);
            }

            const Material* MaterialComponent{ std::as_const(World).GetComponent<Material>(EntityId) };
            const std::uint32_t MaterialFlags{ MaterialComponent == nullptr ? 0u : MaterialComponent->Flags };
            const bool IsPickedHierarchy{ IsEntityWithinPickedHierarchy(World, EntityId, Ctx.PickedEntityId) };
            const std::uint32_t PickFlags{ IsPickedHierarchy ? PickedDrawFlagBitMask : 0u };
            const RegisteredMaterialGroup* ResolvedMaterialGroup{ nullptr };
            if (MaterialGroups.empty() == false) {
                std::size_t ResolvedMaterialGroupIndex{ MaterialComponent == nullptr ? 0u : MaterialComponent->MaterialGroupIndex };
                if (ResolvedMaterialGroupIndex >= MaterialGroups.size() || MaterialGroups[ResolvedMaterialGroupIndex].Items.empty()) {
                    ResolvedMaterialGroupIndex = 0;
                }

                if (ResolvedMaterialGroupIndex < MaterialGroups.size() && MaterialGroups[ResolvedMaterialGroupIndex].Items.empty() == false) {
                    ResolvedMaterialGroup = &MaterialGroups[ResolvedMaterialGroupIndex];
                }
            }

            RFD::ModelContext ModelContext{};
            ModelContext.world = TransformComponent.worldMatrix;
            ModelContext.prevWorld = ModelContext.world;
            ModelContext.flags = SkinnedModelContextFlagBitMask;
            ModelContext.boneIndexStart = GlobalBoneIndexStart;
            ModelContext.objectID = static_cast<std::uint32_t>(RenderData.modelContexts.size());
            RenderData.modelContexts.push_back(ModelContext);

            for (std::size_t SubMeshIndex{ 0 }; SubMeshIndex < SubMeshes.size(); ++SubMeshIndex) {
                const ModelSubMesh& SubMesh{ SubMeshes[SubMeshIndex] };
                const Interface::IPipeline* Pipeline{ nullptr };
                std::uint32_t ResolvedMaterialIndex{ 0 };

                if (ResolvedMaterialGroup != nullptr) {
                    std::size_t ResolvedItemIndex{ SubMesh.MaterialGroupItemIndex };
                    if (ResolvedItemIndex >= ResolvedMaterialGroup->Items.size()) {
                        ResolvedItemIndex = 0;
                    }

                    if (ResolvedItemIndex < ResolvedMaterialGroup->Items.size()) {
                        const RegisteredMaterialGroupItem& RegisteredGroupItem{ ResolvedMaterialGroup->Items[ResolvedItemIndex] };
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
                DrawRecord.flags = MaterialFlags | PickFlags;
                DrawRecord.pad0 = 0;
                RenderData.drawRecords.push_back(DrawRecord);
            }
        }
    }
}
