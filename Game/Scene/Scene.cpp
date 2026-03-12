#include "Scene.h"
#include <algorithm>
#include <array>
#include <cctype>
#include "Asset/Common.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/PickingGizmo.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Events/SelectionEvent.h"
#include "Core/Event/EventQueue.h"
#include "Core/Event/FileDropEvent.h"
#include "Utility/StringUtils.h"

namespace {
    std::string BuildGizmoPrimitiveSelector(float Red, float Green, float Blue) {
        return std::string{ "box;size=1.000000;color=" } + std::to_string(Red) + std::string{ "," } + std::to_string(Green) + std::string{ "," } + std::to_string(Blue) + std::string{ ",1.000000" };
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
        mHierarchyEntitySelectedSubscriptionId{},
        mFileDropSubscriptionId{} {
        mWorldSnapshot.BindReadOnlyWorld(&mWorld.GetReadOnlyView());
        mWorldSnapshot.BindAssetRegistry(&mAssetRegistry);

        mHierarchyEntitySelectedSubscriptionId = Core::Event::Subscribe<Game::HierarchyEntitySelectedEventTag>([this](const Core::Event::Event<Game::HierarchyEntitySelectedEventTag>& HierarchyEntitySelectedEvent) {
            const Game::HierarchyEntitySelectedPayload* Payload{ HierarchyEntitySelectedEvent.GetPayloadAs<Game::HierarchyEntitySelectedPayload>() };

            if (Payload == nullptr) {
                return;
            }

            mFrameContext.PickedEntityId = Payload->SelectedEntityId;
        });

        mFileDropSubscriptionId = Core::Event::Subscribe<Core::Event::FbxBinFileDroppedEventTag>([this](const Core::Event::Event<Core::Event::FbxBinFileDroppedEventTag>& DroppedFileEvent) {
            const Core::Event::FbxBinFileDroppedPayload* Payload{ DroppedFileEvent.GetPayloadAs<Core::Event::FbxBinFileDroppedPayload>() };

            if (Payload == nullptr) {
                return;
            }

            OnFileDropped(Payload->FilePath);
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
        mWorldSnapshot.SetSceneName(mName);
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

            const Arche::EntityID EntityId{ mWorld.CreateEntity() };

            EntityHierarchy Hierarchy{};
            Hierarchy.self = EntityId;
            mWorld.AddComponent(EntityId, Hierarchy);

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
            mWorld.AddComponent(EntityId, PickingGizmoComponent);
        }
    }


    void Scene::OnFileDropped(const std::filesystem::path& FilePath) {
        const std::wstring ExtensionTextWide{ FilePath.extension().wstring() };
        const std::string ExtensionText{ ConvertWstringToUtf8(ExtensionTextWide) };
        std::string LowerExtensionText{ ExtensionText };
        std::transform(LowerExtensionText.begin(), LowerExtensionText.end(), LowerExtensionText.begin(), [](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });

        if (LowerExtensionText != ".bin") {
            return;
        }

        const std::string ModelPath{ FilePath.generic_string() };
        const std::string RootEntityName{ ConvertWstringToUtf8(FilePath.stem().wstring()) };
        const std::uint32_t MaterialGroupIndex{ 0 };
        SpawnModelAtOrigin(ModelPath, RootEntityName, MaterialGroupIndex);
    }

    void Scene::SpawnModelAtOrigin(const std::string& ModelSelector, const std::string& RootEntityName, std::uint32_t MaterialGroupIndex) {
        const std::shared_ptr<Model> ModelData{ mAssetRegistry.GetModel(ModelSelector) };
        if (ModelData == nullptr) {
            return;
        }

        const Model* SourceModel{ ModelData.get() };
        const std::vector<ModelNode>& ModelNodes{ SourceModel->GetNodes() };
        const ModelNode* RootNode{ SourceModel->GetRootNode() };
        if (RootNode == nullptr || ModelNodes.empty()) {
            return;
        }

        const std::size_t RootNodeIndex{ static_cast<std::size_t>(RootNode - ModelNodes.data()) };
        std::vector<Arche::EntityID> NodeEntities(ModelNodes.size(), Arche::NullEntityID);

        for (std::size_t NodeIndex{ 0 }; NodeIndex < ModelNodes.size(); ++NodeIndex) {
            NodeEntities[NodeIndex] = mWorld.CreateEntity();
        }

        for (std::size_t NodeIndex{ 0 }; NodeIndex < ModelNodes.size(); ++NodeIndex) {
            Transform NodeTransform{};
            NodeTransform.nodeToParent = ModelNodes[NodeIndex].GetNodeToParent();
            NodeTransform.geometryToNode = ModelNodes[NodeIndex].GetGeometryToNode();
            if (NodeIndex == RootNodeIndex) {
                NodeTransform.position = SimpleMath::Vector3{ 0.0f, 0.0f, 0.0f };
            }

            mWorld.AddComponent(NodeEntities[NodeIndex], NodeTransform);

            const bool HasRenderableGeometry{ ModelNodes[NodeIndex].GetSubMeshes().empty() == false };
            if (HasRenderableGeometry) {
                StaticMeshRenderer NodeRenderer{};
                NodeRenderer.model = ModelData.get();
                NodeRenderer.nodeIndex = static_cast<std::uint32_t>(NodeIndex);
                NodeRenderer.materialGroupIndex = MaterialGroupIndex;
                NodeRenderer.active = true;
                mWorld.AddComponent(NodeEntities[NodeIndex], NodeRenderer);
            }

            if (HasRenderableGeometry) {
                BoundingBox NodeBoundingBox{};
                NodeBoundingBox.UpdateFromModel(ModelData.get(), static_cast<std::uint32_t>(NodeIndex));
                mWorld.AddComponent(NodeEntities[NodeIndex], NodeBoundingBox);
            }

            EntityHierarchy Hierarchy{};
            Hierarchy.self = NodeEntities[NodeIndex];
            mWorld.AddComponent(NodeEntities[NodeIndex], Hierarchy);

            std::string NodeNameText{ ModelNodes[NodeIndex].GetName() };
            if (NodeIndex == RootNodeIndex && RootEntityName.empty() == false) {
                NodeNameText = RootEntityName;
            }

            if (NodeNameText.empty()) {
                NodeNameText = std::string{ "DroppedModelNode_" } + std::to_string(NodeIndex);
            }
            const Name NodeName{ CreateNameComponent(NodeNameText) };
            mWorld.AddComponent(NodeEntities[NodeIndex], NodeName);
        }

        for (std::size_t NodeIndex{ 0 }; NodeIndex < ModelNodes.size(); ++NodeIndex) {
            EntityHierarchy* ParentHierarchy{ mWorld.GetComponent<EntityHierarchy>(NodeEntities[NodeIndex]) };
            if (ParentHierarchy == nullptr) {
                continue;
            }

            const std::vector<std::uint32_t>& Children{ ModelNodes[NodeIndex].GetChildren() };
            Arche::EntityID PreviousChild{ Arche::NullEntityID };

            for (std::uint32_t ChildNodeIndex : Children) {
                if (ChildNodeIndex >= NodeEntities.size()) {
                    continue;
                }

                EntityHierarchy* ChildHierarchy{ mWorld.GetComponent<EntityHierarchy>(NodeEntities[ChildNodeIndex]) };
                if (ChildHierarchy == nullptr) {
                    continue;
                }

                ChildHierarchy->parent = NodeEntities[NodeIndex];
                if (ParentHierarchy->firstChild == Arche::NullEntityID) {
                    ParentHierarchy->firstChild = NodeEntities[ChildNodeIndex];
                }

                if (PreviousChild != Arche::NullEntityID) {
                    EntityHierarchy* PreviousHierarchy{ mWorld.GetComponent<EntityHierarchy>(PreviousChild) };
                    if (PreviousHierarchy != nullptr) {
                        PreviousHierarchy->nextSibling = NodeEntities[ChildNodeIndex];
                    }
                }

                PreviousChild = NodeEntities[ChildNodeIndex];
            }
        }
    }

    void Scene::InitializeWorldSnapshot() {
        mWorldSnapshot.BindReadOnlyWorld(&mWorld.GetReadOnlyView());
        mWorldSnapshot.BindAssetRegistry(&mAssetRegistry);
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
        mWorldSnapshot.SetSceneName(mName);

        for (const std::unique_ptr<ISystem>& SystemInstance : mSystems) {
            if (SystemInstance == nullptr) {
                continue;
            }

            mWorldSnapshot.AddSystemName(SystemInstance->Name());
        }

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
