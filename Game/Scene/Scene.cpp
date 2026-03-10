#include "Scene.h"
#include <array>
#include "Asset/Common.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/PickingGizmo.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Events/SelectionEvent.h"
#include "Core/Event/EventQueue.h"

namespace {
    std::string BuildGizmoPrimitiveSelector(float Red, float Green, float Blue) {
        return std::string{ "box;size=1.000000;color=" } + std::to_string(Red) + std::string{ "," } + std::to_string(Green) + std::string{ "," } + std::to_string(Blue) + std::string{ ",1.000000" };
    }

    const char* ResolveAxisName(std::uint32_t AxisIndex) {
        if (AxisIndex == 0) {
            return "X";
        }

        if (AxisIndex == 1) {
            return "Y";
        }

        return "Z";
    }

    std::uint32_t CreateGizmoMaterialGroup(Game::AssetRegistry& AssetRegistry) {
        asset::MaterialGroup MaterialGroup{};
        MaterialGroup.Name = "PickingGizmoMaterialGroup";

        asset::MaterialGroupItem Item{};
        Item.PipelineName = "PrimitiveVertexColorGraphics";
        MaterialGroup.Items.push_back(Item);

        return AssetRegistry.AddMaterialGroup(MaterialGroup);
    }

}

namespace Game {
    Scene::Scene()
        : mName{},
        mWorld{},
        mFrameContext{},
        mAssetRegistry{},
        mSystems{},
        mSystemSceduler{},
        mWorldSnapshot{},
        mWorldSnapshotVersion{},
        mHierarchyEntitySelectedSubscriptionId{} {
        mWorldSnapshot.BindReadOnlyWorld(&mWorld.GetReadOnlyView());

        mHierarchyEntitySelectedSubscriptionId = Core::Event::Subscribe<Game::HierarchyEntitySelectedEventTag>([this](const Core::Event::Event<Game::HierarchyEntitySelectedEventTag>& HierarchyEntitySelectedEvent) {
            const Game::HierarchyEntitySelectedPayload* Payload{ HierarchyEntitySelectedEvent.GetPayloadAs<Game::HierarchyEntitySelectedPayload>() };

            if (Payload == nullptr) {
                return;
            }

            mFrameContext.PickedEntityId = Payload->SelectedEntityId;
        });
    }

    Scene::~Scene() {
    }

    Arche::World& Scene::GetWorld() {
        return mWorld;
    }

    const Arche::World& Scene::GetWorld() const {
        return mWorld;
    }

    FrameContext& Scene::GetFrameContext() {
        return mFrameContext;
    }

    const FrameContext& Scene::GetFrameContext() const {
        return mFrameContext;
    }

    RFD::RenderFrameData& Scene::GetRenderFrameData() {
        return mFrameContext.RenderData;
    }

    const RFD::RenderFrameData& Scene::GetRenderFrameData() const {
        return mFrameContext.RenderData;
    }

    AssetRegistry& Scene::GetAssetRegistry() {
        return mAssetRegistry;
    }

    const AssetRegistry& Scene::GetAssetRegistry() const {
        return mAssetRegistry;
    }

    void Scene::InitializeAssetRegistry(ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Core::DX::DescriptorHeap* SrvHeap) {
        mAssetRegistry.Initialize(Device, CopyQueue, Allocator);
        mAssetRegistry.SetSrvHeap(SrvHeap);
        mFrameContext.MaterialGroups = &mAssetRegistry.GetMaterialGroups();
        mFrameContext.RenderData.materials = mAssetRegistry.GetPackedMaterials();
        mFrameContext.RenderData.materialTextureTable = mAssetRegistry.GetMaterialTextureTable();

        InitializePickingGizmoEntities();
    }

    void Scene::SetName(const std::string& NewName) {
        mName = NewName;
    }

    const std::string& Scene::GetName() const {
        return mName;
    }

    void Scene::AddSystem(std::unique_ptr<ISystem> NewSystem) {
        if (NewSystem == nullptr) {
            return;
        }

        mSystems.push_back(std::move(NewSystem));
    }

    void Scene::BuildSystemExecutionPlan() {
        mSystemSceduler.BuildExecutionPlan(mSystems);
    }

    void Scene::PrepareRender() {
        mAssetRegistry.PrepareRenderTextures(mFrameContext.RenderData);
        mFrameContext.RenderData.materialTextureTable = mAssetRegistry.GetMaterialTextureTable();
    }

    void Scene::InitializePickingGizmoEntities() {
        for (const auto [PickingGizmoComponent] : mWorld.Query<PickingGizmo>()) {
            (void)PickingGizmoComponent;
            return;
        }

        const std::uint32_t GizmoMaterialGroupIndex{ CreateGizmoMaterialGroup(mAssetRegistry) };
        const std::array<std::array<float, 3>, 3> AxisColors{ { { 1.0f, 0.2f, 0.2f }, { 0.2f, 1.0f, 0.2f }, { 0.2f, 0.4f, 1.0f } } };

        for (std::uint32_t AxisIndex{ 0 }; AxisIndex < 3; ++AxisIndex) {
            const std::array<float, 3>& AxisColor{ AxisColors[AxisIndex] };
            const std::string Selector{ BuildGizmoPrimitiveSelector(AxisColor[0], AxisColor[1], AxisColor[2]) };
            const std::shared_ptr<Model> GizmoModel{ mAssetRegistry.GetModel(Selector) };
            if (GizmoModel == nullptr) {
                continue;
            }

            for (std::uint32_t DirectionIndex{ 0 }; DirectionIndex < 2; ++DirectionIndex) {
                const Arche::EntityID EntityId{ mWorld.CreateEntity() };

                EntityHierarchy Hierarchy{};
                Hierarchy.self = EntityId;
                mWorld.AddComponent(EntityId, Hierarchy);

                const char* AxisName{ ResolveAxisName(AxisIndex) };
                const std::string DirectionName{ DirectionIndex == 0 ? std::string{ "Negative" } : std::string{ "Positive" } };
                const std::string GizmoNameText{ std::string{ "Gizmo_" } + AxisName + std::string{ "_" } + DirectionName };
                const Name GizmoName{ CreateNameComponent(GizmoNameText) };
                mWorld.AddComponent(EntityId, GizmoName);

                Transform TransformComponent{};
                mWorld.AddComponent(EntityId, TransformComponent);

                StaticMeshRenderer MeshRenderer{};
                MeshRenderer.model = GizmoModel.get();
                MeshRenderer.materialGroupIndex = GizmoMaterialGroupIndex;
                MeshRenderer.active = false;
                mWorld.AddComponent(EntityId, MeshRenderer);

                BoundingBox GizmoBoundingBox{};
                mWorld.AddComponent(EntityId, GizmoBoundingBox);

                PickingGizmo PickingGizmoComponent{};
                PickingGizmoComponent.axisIndex = AxisIndex;
                PickingGizmoComponent.directionSign = DirectionIndex == 0 ? -1.0f : 1.0f;
                mWorld.AddComponent(EntityId, PickingGizmoComponent);
            }
        }
    }

    void Scene::InitializeWorldSnapshot() {
        mWorldSnapshot.BindReadOnlyWorld(&mWorld.GetReadOnlyView());
        RebuildWorldSnapshot();
        mWorldSnapshotVersion = mWorld.GetStructureVersion();
    }

    void Scene::UpdateWorldSnapshotIfNeeded() {
        const std::uint64_t CurrentStructureVersion{ mWorld.GetStructureVersion() };

        if (CurrentStructureVersion == mWorldSnapshotVersion) {
            return;
        }

        RebuildWorldSnapshot();
        mWorldSnapshotVersion = CurrentStructureVersion;
    }

    const SceneWorldSnapshot& Scene::GetWorldSnapshot() const {
        return mWorldSnapshot;
    }

    void Scene::RebuildWorldSnapshot() {
        mWorldSnapshot.Clear();


        for (const auto& [NameComponent, HierarchyComponent] : mWorld.Query<Name, EntityHierarchy>()) {
            if (GetNameText(NameComponent)[0] == '\0') {
                continue;
            }

            mWorldSnapshot.AddEntity(HierarchyComponent.self, HierarchyComponent.parent);
        }

        mWorldSnapshot.BuildHierarchy();
    }

    void Scene::ExecutePhase(Phase TargetPhase, float Dt) {
        if (TargetPhase == Phase::PreUpdate) {
            mFrameContext.RenderData.modelContexts.clear();
            mFrameContext.RenderData.drawRecords.clear();
            mFrameContext.RenderData.materials = mAssetRegistry.GetPackedMaterials();
            mFrameContext.RenderData.materialTextureTable = mAssetRegistry.GetMaterialTextureTable();
        }

        const SystemSceduler::PhaseBatchArray* PhaseBatches{ mSystemSceduler.GetPhaseBatches(TargetPhase) };
        if (PhaseBatches == nullptr) {
            return;
        }

        for (const SystemSceduler::SystemBatch& Batch : *PhaseBatches) {
            for (ISystem* System : Batch) {
                System->Execute(mWorld, mFrameContext, Dt);
            }
        }
    }
}
