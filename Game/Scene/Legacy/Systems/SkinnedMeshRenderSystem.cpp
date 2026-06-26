#include "SkinnedMeshRenderSystem.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>
#include "Game/Model/AssetRegistry.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Culling.h"
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

    bool IsVisibleByShadowBox(Arche::World& World, Arche::EntityID EntityId, const DirectX::BoundingOrientedBox& CullingBox) {
        const Game::Culling* CullingComponent{ std::as_const(World).GetComponent<Game::Culling>(EntityId) };
        if (CullingComponent != nullptr && CullingComponent->frustumCulling == false) {
            return true;
        }

        const Game::BoundingBox* BoundingBoxComponent{ std::as_const(World).GetComponent<Game::BoundingBox>(EntityId) };
        if (BoundingBoxComponent == nullptr || BoundingBoxComponent->HasWorldObb() == false) {
            return true;
        }

        return CullingBox.Intersects(BoundingBoxComponent->GetWorldObb());
    }

    bool IsDrawBoundingBoxesEnabled(const RenderContract::RenderFrameData& RenderData) {
        return (RenderData.mFrameGlobals.mFlags & RenderContract::FrameGlobalFlagDrawBoundingBoxes) != 0u;
    }

    void AppendBoundingBoxContext(const DirectX::BoundingOrientedBox& WorldObb, RenderContract::RenderFrameData& RenderData) {
        if (IsDrawBoundingBoxesEnabled(RenderData) == false) {
            return;
        }

        RenderContract::BoundingBoxContext BoundingBoxContext{};
        BoundingBoxContext.mCenter = SimpleMath::Vector4{ WorldObb.Center.x, WorldObb.Center.y, WorldObb.Center.z, 1.0f };
        BoundingBoxContext.mExtents = SimpleMath::Vector4{ WorldObb.Extents.x, WorldObb.Extents.y, WorldObb.Extents.z, 0.0f };
        BoundingBoxContext.mOrientation = SimpleMath::Vector4{ WorldObb.Orientation.x, WorldObb.Orientation.y, WorldObb.Orientation.z, WorldObb.Orientation.w };
        RenderData.mBoundingBoxContexts.push_back(BoundingBoxContext);
    }

    void AppendSkinnedDrawRecords(const std::vector<Game::ModelSubMesh>& SubMeshes, const Game::ModelNode& Node, const Game::RegisteredMaterialGroup* ResolvedMaterialGroup, std::uint32_t ObjectIndex, std::uint32_t MaterialFlags, std::uint32_t PickFlags, std::vector<RenderContract::DrawRecord>& OutDrawRecords) {
        for (std::size_t SubMeshIndex{ 0 }; SubMeshIndex < SubMeshes.size(); ++SubMeshIndex) {
            const Game::ModelSubMesh& SubMesh{ SubMeshes[SubMeshIndex] };
            const RenderContract::IPipeline* Pipeline{ nullptr };
            std::uint32_t ResolvedMaterialIndex{ 0 };

            if (ResolvedMaterialGroup != nullptr) {
                std::size_t ResolvedItemIndex{ SubMesh.mMaterialGroupItemIndex };
                if (ResolvedItemIndex >= ResolvedMaterialGroup->Items.size()) {
                    ResolvedItemIndex = 0;
                }

                if (ResolvedItemIndex < ResolvedMaterialGroup->Items.size()) {
                    const Game::RegisteredMaterialGroupItem& RegisteredGroupItem{ ResolvedMaterialGroup->Items[ResolvedItemIndex] };
                    Pipeline = RegisteredGroupItem.Pipeline;
                    ResolvedMaterialIndex = RegisteredGroupItem.MaterialIndex;
                }
            }

            RenderContract::DrawRecord DrawRecord{};
            DrawRecord.mPipeline = Pipeline;
            DrawRecord.mMesh = &Node;
            DrawRecord.mSubMesh = static_cast<std::uint32_t>(SubMeshIndex);
            DrawRecord.mPass = 0;
            DrawRecord.mObjectIndex = ObjectIndex;
            DrawRecord.mMaterialIndex = ResolvedMaterialIndex;
            DrawRecord.mFlags = MaterialFlags | PickFlags;
            DrawRecord.mPadding0 = 0;
            OutDrawRecords.push_back(DrawRecord);
        }
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
        static std::array<ComponentAccess, 5> Accesses{ { { typeid(SkinnedMeshRenderer), Access::Read }, { typeid(Transform), Access::Read }, { typeid(EntityHierarchy), Access::Read }, { typeid(BoundingBox), Access::Read }, { typeid(Culling), Access::Read } } };
        return Accesses;
    }

    std::span<const ResourceAccess> SkinnedMeshRenderSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 4> Accesses{ { { typeid(RenderContract::RenderFrameData), Access::Write }, { typeid(std::vector<RegisteredMaterialGroup>), Access::Read }, { typeid(Arche::EntityID), Access::Read }, { typeid(std::vector<SkinnedMeshPreparedData>), Access::Read } } };
        return Accesses;
    }

    void SkinnedMeshRenderSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Dt;

        RenderContract::RenderFrameData& RenderData{ Ctx.RenderData };
        const std::vector<RegisteredMaterialGroup>& MaterialGroups{ *Ctx.MaterialGroups };
        const std::uint32_t ShadowCascadeCount{ RenderContract::ResolveShadowCascadeCount(RenderData.mShadowMappingParameter) };
        const std::array<DirectX::BoundingOrientedBox, RenderContract::ShadowCascadeMaxCount> ShadowCullingBoxes{ RenderContract::BuildShadowCullingBoxes(RenderData.mShadowMappingParameter) };
        const bool IsDrawBoundingBoxesEnabledForFrame{ IsDrawBoundingBoxesEnabled(RenderData) };
        std::unordered_map<Arche::EntityID, const SkinnedMeshPreparedData*> PreparedDataByEntity{};
        PreparedDataByEntity.reserve(Ctx.SkinnedMeshPreparedDataItems.size());
        for (const SkinnedMeshPreparedData& PreparedData : Ctx.SkinnedMeshPreparedDataItems) {
            PreparedDataByEntity[PreparedData.EntityId] = &PreparedData;
        }

        std::unordered_set<Arche::EntityID> AppendedBoundingBoxEntities{};
        if (IsDrawBoundingBoxesEnabledForFrame == true) {
            for (auto [AnimatorComponent, BoundingBoxComponent, HierarchyComponent] : World.Query<Animator, BoundingBox, EntityHierarchy>()) {
                (void)AnimatorComponent;
                if (BoundingBoxComponent.HasWorldObb() == false) {
                    continue;
                }

                const Arche::EntityID EntityId{ HierarchyComponent.self };
                const bool IsInserted{ AppendedBoundingBoxEntities.insert(EntityId).second };
                if (IsInserted == false) {
                    continue;
                }

                AppendBoundingBoxContext(BoundingBoxComponent.GetWorldObb(), RenderData);
            }
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

            const std::uint32_t GlobalBoneIndexStart{ static_cast<std::uint32_t>(RenderData.mBonePalette.size()) };
            RenderData.mBonePalette.insert(RenderData.mBonePalette.end(), PreparedData.BonePalette.begin(), PreparedData.BonePalette.end());
            if (IsDrawBoundingBoxesEnabledForFrame == true) {
                RenderData.mBoundingBoxContexts.insert(RenderData.mBoundingBoxContexts.end(), PreparedData.BoneBoundingBoxContexts.begin(), PreparedData.BoneBoundingBoxContexts.end());
            }

            BoundingBox* BoundingBoxComponent{ World.GetComponent<BoundingBox>(EntityId) };
            if (IsDrawBoundingBoxesEnabledForFrame == true && BoundingBoxComponent != nullptr && BoundingBoxComponent->HasWorldObb() == true) {
                const bool IsInserted{ AppendedBoundingBoxEntities.insert(EntityId).second };
                if (IsInserted == true) {
                    AppendBoundingBoxContext(BoundingBoxComponent->GetWorldObb(), RenderData);
                }
            }

            const Material* MaterialComponent{ std::as_const(World).GetComponent<Material>(EntityId) };
            const std::uint32_t MaterialFlags{ MaterialComponent == nullptr ? 0u : MaterialComponent->Flags };
            const bool IsPickedHierarchy{ IsEntityWithinPickedHierarchy(World, EntityId, Ctx.PickedEntityId) };
            const std::uint32_t PickFlags{ IsPickedHierarchy ? PickedDrawFlagBitMask : 0u };
            const RegisteredMaterialGroup* ResolvedMaterialGroup{ ResolveMaterialGroup(MaterialGroups, MaterialComponent) };

            RenderContract::ModelContext ModelContext{};
            ModelContext.mWorld = TransformComponent.worldMatrix;
            ModelContext.mPrevWorld = TransformComponent.mPrevWorldMatrix;
            ModelContext.mFlags = SkinnedModelContextFlagBitMask;
            ModelContext.mBoneIndexStart = GlobalBoneIndexStart;
            ModelContext.mObjectId = static_cast<std::uint32_t>(RenderData.mModelContexts.size());
            RenderData.mModelContexts.push_back(ModelContext);
            AppendSkinnedDrawRecords(SubMeshes, Node, ResolvedMaterialGroup, ModelContext.mObjectId, MaterialFlags, PickFlags, RenderData.mDrawRecords);

            for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1) {
                const bool IsVisibleByShadow{ IsVisibleByShadowBox(World, EntityId, ShadowCullingBoxes[CascadeIndex]) };
                if (IsVisibleByShadow == false) {
                    continue;
                }

                RenderContract::ShadowRenderContext& ShadowRenderContext{ RenderData.mShadowRenderContexts[CascadeIndex] };
                RenderContract::ModelContext ShadowModelContext{};
                ShadowModelContext.mWorld = TransformComponent.worldMatrix;
                ShadowModelContext.mPrevWorld = TransformComponent.mPrevWorldMatrix;
                ShadowModelContext.mFlags = SkinnedModelContextFlagBitMask;
                ShadowModelContext.mBoneIndexStart = GlobalBoneIndexStart;
                ShadowModelContext.mObjectId = static_cast<std::uint32_t>(ShadowRenderContext.mModelContexts.size());
                ShadowRenderContext.mModelContexts.push_back(ShadowModelContext);
                AppendSkinnedDrawRecords(SubMeshes, Node, ResolvedMaterialGroup, ShadowModelContext.mObjectId, MaterialFlags, 0u, ShadowRenderContext.mDrawRecords);
            }
        }
    }
}
