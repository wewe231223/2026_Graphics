#include "Scene.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_set>
#include "Asset/Common.h"

#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/PickingGizmo.h"
#include "Game/Scene/Components/PrefabInstance.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Components/ComponentLuaTypeDefinitions.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/Frustum.h"
#include "Game/Scene/Components/RuntimeVariableTable.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/AnimatorGraphPlayer.h"
#include "Game/Scene/Components/Intents/CameraIntent.h"
#include "Game/Scene/Components/ScriptComponent.h"
#include "Game/Scene/Components/Tags.h"

#include "Game/Scene/Events/SelectionEvent.h"
#include "Core/Event/EventQueue.h"
#include "Core/Event/FileDropEvent.h"
#include "Utility/StringUtils.h"
#include "SceneEntityFactory.h"

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

    std::uint64_t GenerateNextPrefabId(Game::Scene& TargetScene) {
        std::unordered_set<std::uint64_t> UsedPrefabIds{};
        for (const auto [PrefabComponent] : TargetScene.GetWorld().Query<Game::PrefabInstance>()) {
            if (PrefabComponent.PrefabId == 0ull) {
                continue;
            }

            UsedPrefabIds.insert(PrefabComponent.PrefabId);
        }

        std::uint64_t NextPrefabId{ 1001ull };
        while (UsedPrefabIds.contains(NextPrefabId)) {
            NextPrefabId += 1ull;
        }

        return NextPrefabId;
    }
}

namespace Game {
    Scene::Scene()
        : mName{},
        mWorld{},
        mFrameContext{},
        mAssetRegistry{},
        mRuntimeVariableInputTable{},
        mSystems{},
        mSystemSceduler{},
        mWorldSnapshot{},
        mWorldSnapshotVersion{},
        mHierarchyEntitySelectedSubscriptionId{},
        mFileDropSubscriptionId{} {
        mWorldSnapshot.BindReadOnlyWorld(&mWorld.GetReadOnlyView());
        mWorldSnapshot.BindWorld(&mWorld);
        mWorldSnapshot.BindAssetRegistry(&mAssetRegistry);
        mFrameContext.RuntimeVariableInputTableResource = &mRuntimeVariableInputTable;

		mLuaScriptFramework.Initialize(&mWorld);
        mLuaScriptFramework.SetFixedUpdateInterval(1.f); 
        mLuaScriptFramework.OpenDefaultLibraries(); 
        RegisterScriptTypes();

        mHierarchyEntitySelectedSubscriptionId = Core::Event::Subscribe<Game::HierarchyEntitySelectedEventTag>([this](const Core::Event::Event<Game::HierarchyEntitySelectedEventTag>& HierarchyEntitySelectedEvent) {
            const Game::HierarchyEntitySelectedPayload* Payload{ HierarchyEntitySelectedEvent.GetPayloadAs<Game::HierarchyEntitySelectedPayload>() };

            if (Payload == nullptr) {
                return;
            }

            mFrameContext.PickedEntityId = Payload->SelectedEntityId;

            Game::PickedEntityChangedPayload PickedEntityChangedPayload{};
            PickedEntityChangedPayload.PickedEntityId = Payload->SelectedEntityId;
            Core::Event::Enqueue<Game::PickedEntityChangedEventTag, Game::PickedEntityChangedPayload>(std::move(PickedEntityChangedPayload), true);
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

    RuntimeVariableInputTable& Scene::GetRuntimeVariableInputTable() {
        return mRuntimeVariableInputTable;
    }

    const RuntimeVariableInputTable& Scene::GetRuntimeVariableInputTable() const {
        return mRuntimeVariableInputTable;
    }

    void Scene::InitializeAssetRegistry(ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Core::DX::DescriptorHeap* SrvHeap) {
        mAssetRegistry.Initialize(Device, CopyQueue, Allocator);
        mAssetRegistry.SetSrvHeap(SrvHeap);
        mFrameContext.MaterialGroups = &mAssetRegistry.GetMaterialGroups();
        mFrameContext.AssetRegistryResource = &mAssetRegistry;
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
            MeshRenderer.active = false;
            mWorld.AddComponent(EntityId, MeshRenderer);

            Material MaterialComponent{};
            MaterialComponent.MaterialGroupIndex = GizmoMaterialGroupIndex;
            mWorld.AddComponent(EntityId, MaterialComponent);

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
        SpawnModelAtOrigin(ModelPath, RootEntityName, MaterialGroupIndex, false);
    }

    void Scene::SpawnModelAtOrigin(const std::string& ModelSelector, const std::string& RootEntityName, std::uint32_t MaterialGroupIndex, bool IsDerivedEntity) {
        const std::shared_ptr<Model> ModelData{ mAssetRegistry.GetModel(ModelSelector) };
        if (ModelData == nullptr) {
            return;
        }

        SceneEntityFactory EntityFactory{ *this };
        ModelHierarchySpawnRequest SpawnRequest{};
        SpawnRequest.ModelData = ModelData;
        SpawnRequest.RootEntityName = RootEntityName;
        SpawnRequest.MaterialGroupIndex = MaterialGroupIndex;
        SpawnRequest.IsActive = true;
        SpawnRequest.IsDerivedEntity = IsDerivedEntity;

        std::vector<Arche::EntityID> SpawnedEntities{};
        const bool IsSpawned{ EntityFactory.SpawnModelHierarchy(SpawnRequest, &SpawnedEntities) };
        if (IsSpawned == false || SpawnedEntities.empty() == true) {
            return;
        }

        Arche::EntityID RootEntityId{ Arche::NullEntityID };
        for (Arche::EntityID SpawnedEntityId : SpawnedEntities) {
            const EntityHierarchy* Hierarchy{ mWorld.GetComponent<EntityHierarchy>(SpawnedEntityId) };
            if (Hierarchy != nullptr && Hierarchy->parent == Arche::NullEntityID) {
                RootEntityId = SpawnedEntityId;
                break;
            }
        }

        if (RootEntityId == Arche::NullEntityID) {
            return;
        }

        PrefabInstance RootPrefabInstance{};
        RootPrefabInstance.PrefabId = GenerateNextPrefabId(*this);
        mWorld.AddComponent(RootEntityId, RootPrefabInstance);
    }

    void Scene::RegisterScriptTypes() {


        mLuaScriptFramework.RegisterTypeByDefinition<Arche::EntityID>();
        mLuaScriptFramework.RegisterTypeByDefinition<DirectX::SimpleMath::Vector2>();
        mLuaScriptFramework.RegisterTypeByDefinition<DirectX::SimpleMath::Vector3>();
        mLuaScriptFramework.RegisterTypeByDefinition<DirectX::SimpleMath::Quaternion>();
        mLuaScriptFramework.RegisterTypeByDefinition<DirectX::SimpleMath::Matrix>();
        mLuaScriptFramework.RegisterTypeByDefinition<Game::NameTextArray>();
        mLuaScriptFramework.RegisterTypeByDefinition<Game::RuntimeVariableBoolArray>();
        mLuaScriptFramework.RegisterTypeByDefinition<Game::RuntimeVariableIntArray>();
        mLuaScriptFramework.RegisterTypeByDefinition<Game::RuntimeVariableFloatArray>();




        mLuaScriptFramework.RegisterComponentByDefinition<Game::Material>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Name>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Transform>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::EntityHierarchy>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::StaticMeshRenderer>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::SkinnedMeshRenderer>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::BoundingBox>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Bone>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::BoneSkinReference>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Camera>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Frustum>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::RuntimeVariableTable>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Animator>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::AnimatorGraphPlayer>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::PrefabInstance>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::PickingGizmo>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::LocalPlayerTag>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::CameraIntent>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::BehaviorInstanceComponent>();
    }

    void Scene::InitializeWorldSnapshot() {
        mWorldSnapshot.BindReadOnlyWorld(&mWorld.GetReadOnlyView());
        mWorldSnapshot.BindWorld(&mWorld);
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
        switch (TargetPhase) {
            case Phase::PreUpdate:
                mFrameContext.RenderData.modelContexts.clear();
                mFrameContext.RenderData.drawRecords.clear();
                mFrameContext.RenderData.bonePalette.clear();
                mFrameContext.RenderData.materials = mAssetRegistry.GetPackedMaterials();
                mFrameContext.RenderData.materialTextureTable = mAssetRegistry.GetMaterialTextureTable();
                mFrameContext.WorldMatrices.clear();
                mFrameContext.SkinnedPoseCache.clear();
                break;

            case Phase::Update:
                mLuaScriptFramework.Update(Dt);
                break;

            default:
                break;
        }

        const SystemSceduler::PhaseBatchArray* PhaseBatches{ mSystemSceduler.GetPhaseBatches(TargetPhase) };
        if (PhaseBatches == nullptr) {
            switch (TargetPhase) {
                case Phase::Update:
                    mLuaScriptFramework.LateUpdate(Dt);
                    break;

                default:
                    break;
            }

            return;
        }

        for (const SystemSceduler::SystemBatch& Batch : *PhaseBatches) {
            for (ISystem* System : Batch) {
                System->Execute(mWorld, mFrameContext, Dt);
            }
        }

        switch (TargetPhase) {
            case Phase::Update:
                mLuaScriptFramework.LateUpdate(Dt);
                break;

            default:
                break;
        }
    }
}
