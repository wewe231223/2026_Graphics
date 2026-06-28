#include "Scene.h"
#include <utility>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>
#include "Imgui/imgui.h"
#include "Core/Config.h"
#include "Asset/Common.h"

#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/CharacterController.h"
#include "Game/Scene/Components/Culling.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/FootIKRig.h"
#include "Game/Scene/Components/FootIKRuntime.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/PrefabInstance.h"
#include "Game/Scene/Components/PhysicsActor.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/TerrainRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Components/ComponentLuaTypeDefinitions.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/DirectionalLight.h"
#include "Game/Scene/Components/Frustum.h"
#include "Game/Scene/Components/SkySphere.h"
#include "Game/Scene/Components/RuntimeVariableTable.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/AnimatorGraphPlayer.h"
#include "Game/Scene/Components/ScriptComponent.h"
#include "Game/Scene/Components/Tags.h"
#include "Game/Base/Input.h"
#include "Game/Scene/Events/SelectionEvent.h"
#include "Core/Event/EventQueue.h"
#include "Core/Event/FileDropEvent.h"
#include "Utility/MathValidation.h"
#include "Utility/StringUtils.h"
#include "Game/Scene/Base/SceneEntityFactory.h"
#include "Game/Scene/Base/ScenePhysicsRuntimeCoordinator.h"

namespace {
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

    bool TryBuildEnvironmentTerrainInput(Arche::World& World, std::uint32_t FrameIndex, Game::EnvironmentTerrainInput& OutInput) {
        for (auto [TransformComponent, Renderer, HierarchyComponent] : World.Query<Game::Transform, Game::TerrainRenderer, Game::EntityHierarchy>()) {
            static_cast<void>(HierarchyComponent);
            if (Renderer.mResource == nullptr || Renderer.mActive == false || Renderer.mTileMetadataIndex != Game::InvalidTerrainTileMetadataIndex) {
                continue;
            }

            OutInput.mHeightSrvIndex = Renderer.mResource->GetHeightFieldSrvDescriptorIndex(FrameIndex);
            OutInput.mUploadFuture = Renderer.mResource->GetFrameUploadFuture(FrameIndex);
            OutInput.mSplatSrvIndex = Renderer.mResource->GetSplatMapSrvDescriptorIndex(FrameIndex, 0u);
            OutInput.mSplat1SrvIndex = Renderer.mResource->GetSplatMapSrvDescriptorIndex(FrameIndex, 1u);
            OutInput.mWidth = Renderer.mResource->GetHeightFieldWidth();
            OutInput.mHeight = Renderer.mResource->GetHeightFieldHeight();
            OutInput.mSplatWidth = Renderer.mResource->GetSplatMapWidth();
            OutInput.mSplatHeight = Renderer.mResource->GetSplatMapHeight();
            OutInput.mSeed = Renderer.mResource->GetBuildDesc().mProceduralHeightFieldDesc.mSeed;
            OutInput.mPosition = TransformComponent.position;
            OutInput.mScale = TransformComponent.scale;
            OutInput.mCellSizeX = Renderer.mResource->GetCellSizeX();
            OutInput.mCellSizeZ = Renderer.mResource->GetCellSizeZ();
            OutInput.mMaxHeight = Renderer.mResource->GetMaxHeight();
            OutInput.mOriginOffsetX = Renderer.mResource->GetOriginOffsetX();
            OutInput.mOriginOffsetZ = Renderer.mResource->GetOriginOffsetZ();
            return true;
        }

        return false;
    }
}

namespace Game {
    Scene::Scene()
        : mName{},
        mWorld{},
        mPhysicsWorld{},
        mPhysicsRuntime{},
        mPhysicsRuntimeScene{},
        mPhysicsRuntimeSnapshot{},
        mKinematicSceneSimulator{},
        mTerrainManager{},
        mKinematicRuntimeStates{},
        mPhysicsSynchronizationEntityIds{},
        mPhysicsSynchronizationStructureVersion{ std::numeric_limits<std::uint64_t>::max() },
        mPhysicsWorldVersion{ 1U },
        mPhysicsTime{},
        mEnvironmentRuntime{},
        mFrameContext{},
        mAssetRegistry{},
        mSystems{},
        mSystemSceduler{},
        mWorldSnapshot{},
        mWorldSnapshotVersion{},
        mHierarchyEntitySelectedSubscriptionId{},
        mFileDropSubscriptionId{},
        mIsDefaultCameraControlBehaviorAttached{},
        mIsDebugGeometryDrawEnabled{},
        mIsBoundingBoxDrawEnabled{},
        mIsPhysicsRuntimeModeEnabled{ ScenePhysicsRuntimeCoordinator::ResolvePhysicsRuntimeModeEnabled() } {
        InitializePhysicsWorld();

        mWorldSnapshot.BindReadOnlyWorld(&mWorld.GetReadOnlyView());
        mWorldSnapshot.BindWorld(&mWorld);
        mWorldSnapshot.BindAssetRegistry(&mAssetRegistry);

		mLuaScriptFramework.Initialize(&mWorld);
        mLuaScriptFramework.SetFixedUpdateInterval(1.f); 
        mLuaScriptFramework.OpenDefaultLibraries(); 
        RegisterScriptTypes();
        mIsDebugGeometryDrawEnabled = Config::Query()->Get<bool>("Draw_DebugGeometry");

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
        ShutdownPhysicsRuntime();
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

    RenderContract::RenderFrameData& Scene::GetRenderFrameData() {
        return mFrameContext.RenderData;
    }

    const RenderContract::RenderFrameData& Scene::GetRenderFrameData() const {
        return mFrameContext.RenderData;
    }

    void Scene::SetRenderFrameIndex(std::uint32_t RenderFrameIndex) {
        mFrameContext.RenderData.mFrameGlobals.mFrameIndex = RenderFrameIndex;
    }

    AssetRegistry& Scene::GetAssetRegistry() {
        return mAssetRegistry;
    }

    const AssetRegistry& Scene::GetAssetRegistry() const {
        return mAssetRegistry;
    }

    Script::LuaBehaviorFramework& Scene::GetLuaScriptFramework() {
        return mLuaScriptFramework;
    }

    const Script::LuaBehaviorFramework& Scene::GetLuaScriptFramework() const {
        return mLuaScriptFramework;
    }

    PhysicsWorld& Scene::GetPhysicsWorld() {
        return mPhysicsWorld;
    }

    const PhysicsWorld& Scene::GetPhysicsWorld() const {
        return mPhysicsWorld;
    }

    const PhysicsRuntime& Scene::GetPhysicsRuntime() const {
        return mPhysicsRuntime;
    }

    const PhysicsRuntimeScene& Scene::GetPhysicsRuntimeScene() const {
        return mPhysicsRuntimeScene;
    }

    std::uint32_t Scene::GetPhysicsWorldVersion() const {
        return mPhysicsWorldVersion;
    }

    bool Scene::IsPhysicsRuntimeModeEnabled() const {
        return mIsPhysicsRuntimeModeEnabled;
    }

    void Scene::InitializeAssetRegistry(ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Core::DX::DescriptorHeap* SrvHeap, Interface::IComputeQueue* ComputeQueue) {
        mAssetRegistry.Initialize(Device, CopyQueue, Allocator);
        mAssetRegistry.SetSrvHeap(SrvHeap);
        mFrameContext.MaterialGroups = &mAssetRegistry.GetMaterialGroups();
        mFrameContext.AssetRegistryResource = &mAssetRegistry;
        mFrameContext.RenderData.mMaterials = mAssetRegistry.GetPackedMaterials();
        mFrameContext.RenderData.mMaterialTextureTable = mAssetRegistry.GetMaterialTextureTable();

        EnvironmentRuntimeDesc EnvironmentDesc{};
        EnvironmentDesc.mDevice = Device;
        EnvironmentDesc.mAllocator = Allocator;
        EnvironmentDesc.mSrvHeap = SrvHeap;
        EnvironmentDesc.mCopyQueue = CopyQueue;
        EnvironmentDesc.mComputeQueue = ComputeQueue;
        EnvironmentDesc.mPhysicsAdapter = nullptr;
        EnvironmentDesc.mGpuDrivenEnabled = Config::Query()->Get<bool>("EnvironmentObjects_GpuDrivenEnabled");
        mEnvironmentRuntime.Initialize(EnvironmentDesc);
    }

    void Scene::SetEnvironmentConfigPath(const std::string& ConfigPath) {
        mEnvironmentRuntime.SetConfigPath(ConfigPath);
    }

    ScenePhysicsRuntimeContext Scene::BuildPhysicsRuntimeContext() {
        return ScenePhysicsRuntimeContext{ mWorld, mPhysicsWorld, mPhysicsRuntime, mPhysicsRuntimeScene, mPhysicsRuntimeSnapshot, mKinematicSceneSimulator, mTerrainManager, mKinematicRuntimeStates, mPhysicsSynchronizationEntityIds, mPhysicsSynchronizationStructureVersion, mPhysicsWorldVersion, mPhysicsTime, mFrameContext, mWorldSnapshot, mTerrainActorDescBindings, mIsPhysicsRuntimeModeEnabled };
    }

    void Scene::InitializePhysicsWorld() {
        ScenePhysicsRuntimeCoordinator::InitializePhysicsWorld(BuildPhysicsRuntimeContext());
    }

    void Scene::InitializePhysicsRuntime() {
        ScenePhysicsRuntimeCoordinator::InitializePhysicsRuntime(BuildPhysicsRuntimeContext());
    }

    void Scene::ShutdownPhysicsRuntime() {
        ScenePhysicsRuntimeCoordinator::ShutdownPhysicsRuntime(BuildPhysicsRuntimeContext());
    }

    void Scene::SubmitPhysicsRuntimeCommands() {
        ScenePhysicsRuntimeCoordinator::SubmitPhysicsRuntimeCommands(BuildPhysicsRuntimeContext());
    }

    void Scene::PublishPhysicsRuntimeKinematicStates() {
        ScenePhysicsRuntimeCoordinator::PublishPhysicsRuntimeKinematicStates(BuildPhysicsRuntimeContext());
    }

    void Scene::RefreshPhysicsRuntimeSnapshot() {
        ScenePhysicsRuntimeCoordinator::RefreshPhysicsRuntimeSnapshot(BuildPhysicsRuntimeContext());
    }

    void Scene::PublishPhysicsRuntimeStatus(const PhysicsSnapshot* Snapshot) {
        ScenePhysicsRuntimeCoordinator::PublishPhysicsRuntimeStatus(BuildPhysicsRuntimeContext(), Snapshot);
    }

    void Scene::UpdateTerrainManager() {
        ScenePhysicsRuntimeCoordinator::UpdateTerrainManager(BuildPhysicsRuntimeContext());
    }

    void Scene::UpdateSceneKinematicActors(float Dt) {
        ScenePhysicsRuntimeCoordinator::UpdateSceneKinematicActors(BuildPhysicsRuntimeContext(), Dt);
    }

    void Scene::RebuildPhysicsActors() {
        ScenePhysicsRuntimeCoordinator::RebuildPhysicsActors(BuildPhysicsRuntimeContext());
    }

    void Scene::UpdatePhysics(float Dt) {
        ScenePhysicsRuntimeCoordinator::UpdatePhysics(BuildPhysicsRuntimeContext(), Dt);
    }

    void Scene::SetPhysicsTime(Utility::Time* PhysicsTime) {
        mPhysicsTime = PhysicsTime;
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
        PrepareEnvironmentGpuDrivenFrame();

        mAssetRegistry.PrepareRenderTextures(mFrameContext.RenderData);
        mFrameContext.RenderData.mMaterialTextureTable = mAssetRegistry.GetMaterialTextureTable();
    }

    EnvironmentFrameInput Scene::BuildEnvironmentFrameInput() {
        EnvironmentFrameInput EnvironmentInput{};
        EnvironmentInput.mFrameIndex = mFrameContext.RenderData.mFrameGlobals.mFrameIndex;
        EnvironmentInput.mFocusPosition = SimpleMath::Vector3{ mFrameContext.RenderData.mMainCamera.mPosition.x, mFrameContext.RenderData.mMainCamera.mPosition.y, mFrameContext.RenderData.mMainCamera.mPosition.z };
        EnvironmentInput.mView = mFrameContext.RenderData.mFrameGlobals.mView;
        EnvironmentInput.mProjection = mFrameContext.RenderData.mFrameGlobals.mProj;
        EnvironmentInput.mViewProjection = mFrameContext.RenderData.mFrameGlobals.mViewProj;
        TryBuildEnvironmentTerrainInput(mWorld, mFrameContext.RenderData.mFrameGlobals.mFrameIndex, EnvironmentInput.mTerrain);
        return EnvironmentInput;
    }

    void Scene::PrepareEnvironmentGpuDrivenFrame() {
        if (mEnvironmentRuntime.IsGpuDrivenEnabled() == false || mFrameContext.RenderData.mEnvironmentGpuDrivenFrame.mEnabled == true) {
            return;
        }

        mFrameContext.MaterialGroups = &mAssetRegistry.GetMaterialGroups();
        mFrameContext.AssetRegistryResource = &mAssetRegistry;
        mEnvironmentRuntime.Tick(mWorld, mFrameContext, 0.0f);
        const EnvironmentFrameInput EnvironmentInput{ BuildEnvironmentFrameInput() };
        mEnvironmentRuntime.Tick(EnvironmentInput, mFrameContext.RenderData);
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
            const EntityHierarchy* Hierarchy{ std::as_const(mWorld).GetComponent<EntityHierarchy>(SpawnedEntityId) };
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
        mLuaScriptFramework.RegisterGlobalFunction("IsInputKeyDown", &Globals::IsInputKeyDown);
        mLuaScriptFramework.RegisterGlobalFunction("IsInputKeyPressed", &Globals::IsInputKeyPressed);
        mLuaScriptFramework.RegisterGlobalFunction("IsInputKeyReleased", &Globals::IsInputKeyReleased);
        mLuaScriptFramework.RegisterGlobalFunction("GetInputMousePositionX", &Globals::GetInputMousePositionX);
        mLuaScriptFramework.RegisterGlobalFunction("GetInputMousePositionY", &Globals::GetInputMousePositionY);
        mLuaScriptFramework.RegisterGlobalFunction("GetInputMouseDeltaX", &Globals::GetInputMouseDeltaX);
        mLuaScriptFramework.RegisterGlobalFunction("GetInputMouseDeltaY", &Globals::GetInputMouseDeltaY);
        mLuaScriptFramework.RegisterGlobalFunction("IsInputMouseLeftButtonDown", &Globals::IsInputMouseLeftButtonDown);
        mLuaScriptFramework.RegisterGlobalFunction("IsInputMouseRightButtonDown", &Globals::IsInputMouseRightButtonDown);
        mLuaScriptFramework.RegisterGlobalFunction("IsInputMouseMiddleButtonDown", &Globals::IsInputMouseMiddleButtonDown);
        mLuaScriptFramework.RegisterGlobalFunction("GetInputMouseWheelDelta", &Globals::GetInputMouseWheelDelta);
        mLuaScriptFramework.RegisterGlobalFunction("GetActiveCameraForwardDirection", [this]() -> DirectX::SimpleMath::Vector3 {
            for (auto [TransformComponent, CameraComponent] : mWorld.Query<Transform, Camera>()) {
                if (CameraComponent.isActive == false) {
                    continue;
                }

                return TransformComponent.GetForwardDirection();
            }

            return DirectX::SimpleMath::Vector3::Forward;
        });
        mLuaScriptFramework.RegisterGlobalFunction("GetActiveCameraRightDirection", [this]() -> DirectX::SimpleMath::Vector3 {
            for (auto [TransformComponent, CameraComponent] : mWorld.Query<Transform, Camera>()) {
                if (CameraComponent.isActive == false) {
                    continue;
                }

                return TransformComponent.TransformDirectionToWorld(DirectX::SimpleMath::Vector3::Right);
            }

            return DirectX::SimpleMath::Vector3::Right;
        });
        mLuaScriptFramework.RegisterGlobalFunction("GetActiveCameraFlags", [this]() -> std::uint32_t {
            for (auto [CameraComponent] : mWorld.Query<Camera>()) {
                if (CameraComponent.isActive == false) {
                    continue;
                }

                return CameraComponent.cameraFlags;
            }

            return CameraFlagNone;
        });
        mLuaScriptFramework.RegisterGlobalFunction("RaycastTerrainDistance", [this](const DirectX::SimpleMath::Vector3& RayStartPoint, const DirectX::SimpleMath::Vector3& RayDirection, const float RayLength) -> float {
            if (MathUtility::IsFiniteVector3(RayStartPoint) == false || MathUtility::IsFiniteVector3(RayDirection) == false || MathUtility::IsFiniteFloat(RayLength) == false || RayLength <= 0.0f) {
                return -1.0f;
            }

            const DirectX::SimpleMath::Ray Ray{ RayStartPoint, RayDirection };
            DirectX::SimpleMath::Vector3 HitPosition{};
            DirectX::SimpleMath::Vector3 HitNormal{ DirectX::SimpleMath::Vector3::Up };
            float HitDistance{};
            const bool HasHit{ mTerrainManager.TryRaycast(Ray, RayLength, HitPosition, HitNormal, HitDistance) };
            return HasHit == true ? HitDistance : -1.0f;
        });

        mLuaScriptFramework.RegisterTypeByDefinition<Arche::EntityID>();
        mLuaScriptFramework.RegisterTypeByDefinition<DirectX::SimpleMath::Vector2>();
        mLuaScriptFramework.RegisterTypeByDefinition<DirectX::SimpleMath::Vector3>();
        mLuaScriptFramework.RegisterTypeByDefinition<DirectX::SimpleMath::Quaternion>();
        mLuaScriptFramework.RegisterTypeByDefinition<DirectX::SimpleMath::Matrix>();
        mLuaScriptFramework.RegisterTypeUsertype<Game::ComponentTextArray>("Text", sol::constructors<Game::ComponentTextArray()>(), sol::meta_function::index, [](const Game::ComponentTextArray& TargetArray, std::size_t LuaIndex) -> Game::ComponentTextArray::value_type { if (LuaIndex >= TargetArray.size()) { return '\0'; } return TargetArray[LuaIndex]; }, sol::meta_function::new_index, [](Game::ComponentTextArray& TargetArray, std::size_t LuaIndex, const Game::ComponentTextArray::value_type Value) { if (LuaIndex >= TargetArray.size()) { return; } TargetArray[LuaIndex] = Value; }, "Get", [](const Game::ComponentTextArray& TargetArray, const std::size_t Index) -> Game::ComponentTextArray::value_type { if (Index >= TargetArray.size()) { return '\0'; } return TargetArray[Index]; }, "Set", [](Game::ComponentTextArray& TargetArray, const std::size_t Index, const Game::ComponentTextArray::value_type Value) { if (Index >= TargetArray.size()) { return; } TargetArray[Index] = Value; }, "Size", [](const Game::ComponentTextArray& TargetArray) -> std::size_t { return TargetArray.size(); });
        mLuaScriptFramework.RegisterTypeByDefinition<Game::RuntimeVariableBoolArray>();
        mLuaScriptFramework.RegisterTypeByDefinition<Game::RuntimeVariableIntArray>();
        mLuaScriptFramework.RegisterTypeByDefinition<Game::RuntimeVariableFloatArray>();




        mLuaScriptFramework.RegisterComponentByDefinition<Game::Material>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Name>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Transform>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::EntityHierarchy>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::StaticMeshRenderer>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::TerrainRenderer>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::SkinnedMeshRenderer>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Culling>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::BoundingBox>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Bone>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::BoneSkinReference>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Camera>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::DirectionalLight>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Frustum>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::SkySphere>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::RuntimeVariableTable>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Animator>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::AnimatorGraphPlayer>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::FootIKRig>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::FootIKRuntime>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::PrefabInstance>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::PhysicsActorSettings>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::PhysicsActor>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::CharacterController>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::Tag>();
        mLuaScriptFramework.RegisterComponentByDefinition<Game::BehaviorInstanceComponent>();
    }

    void Scene::AddTerrainActorDesc(Arche::EntityID EntityId, const PhysicsTerrainActor::ActorDesc& TerrainActorDesc) {
        ScenePhysicsRuntimeCoordinator::AddTerrainActorDesc(BuildPhysicsRuntimeContext(), EntityId, TerrainActorDesc);
    }

    void Scene::ClearTerrainActorDescs() {
        ScenePhysicsRuntimeCoordinator::ClearTerrainActorDescs(BuildPhysicsRuntimeContext());
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

    void Scene::UpdateCameraVirtualMouseState() {
        Globals::Input& InputInstance{ Globals::Input::Get() };
        const bool IsDebugGeometryToggleRequested{ InputInstance.IsKeyPressed(DirectX::Keyboard::Keys::F3) };
        const bool IsThirdPersonToggleRequested{ InputInstance.IsKeyPressed(DirectX::Keyboard::Keys::F8) };
        const bool IsBoundingBoxToggleRequested{ InputInstance.IsKeyPressed(DirectX::Keyboard::Keys::F9) };
        const DirectX::Mouse::ButtonStateTracker& MouseTracker{ InputInstance.GetMouseTracker() };
        const bool IsSelectionDragInput{ mFrameContext.PickedEntityId != Arche::NullEntityID && (MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::PRESSED || MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::HELD) };

        if (IsDebugGeometryToggleRequested) {
            mIsDebugGeometryDrawEnabled = (mIsDebugGeometryDrawEnabled == false);
        }

        if (IsBoundingBoxToggleRequested) {
            mIsBoundingBoxDrawEnabled = (mIsBoundingBoxDrawEnabled == false);
        }

        bool IsUiCapturingInput{ false };
        if (Config::Query()->Get<bool>("Block_ImGui") == false) {
            const ImGuiIO& ImGuiInputState{ ImGui::GetIO() };
            const bool IsUiCapturingMouseInput{ ImGuiInputState.WantCaptureMouse };
            const bool IsUiCapturingKeyboardInput{ ImGuiInputState.WantCaptureKeyboard };
            IsUiCapturingInput = IsUiCapturingMouseInput || IsUiCapturingKeyboardInput;
        }

        for (auto [CameraComponent] : mWorld.Query<Camera>()) {
            if (CameraComponent.isActive == false) {
                continue;
            }

            if (IsThirdPersonToggleRequested) {
                const bool IsThirdPersonMode{ (CameraComponent.cameraFlags & CameraFlagThirdPerson) != 0u };
                if (IsThirdPersonMode) {
                    CameraComponent.cameraFlags &= ~CameraFlagThirdPerson;
                    CameraComponent.cameraFlags |= CameraFlagFreeLook;
                    InputInstance.SetRightButtonVirtualMouseEnabled(true);
                    InputInstance.SetVirtualMouse(false);
                }
                else {
                    CameraComponent.cameraFlags |= CameraFlagThirdPerson;
                    CameraComponent.cameraFlags &= ~CameraFlagFreeLook;
                    InputInstance.SetRightButtonVirtualMouseEnabled(false);
                    InputInstance.SetVirtualMouse(true);
                }
            }

            if (IsSelectionDragInput || IsUiCapturingInput) {
                return;
            }

            const bool IsThirdPersonMode{ (CameraComponent.cameraFlags & CameraFlagThirdPerson) != 0u };
            if (IsThirdPersonMode) {
                InputInstance.SetRightButtonVirtualMouseEnabled(false);
                InputInstance.SetVirtualMouse(true);
            }
            else {
                InputInstance.SetRightButtonVirtualMouseEnabled(true);
            }

            return;
        }
    }

    void Scene::AttachDefaultCameraControlBehavior() {
        if (mIsDefaultCameraControlBehaviorAttached) {
            return;
        }

        for (const auto [CameraComponent, HierarchyComponent] : mWorld.Query<Camera, EntityHierarchy>()) {
            if (CameraComponent.isActive == false) {
                continue;
            }

            const Arche::EntityID TargetCameraEntity{ HierarchyComponent.self };
            const BehaviorInstanceComponent* ExistingBehaviorComponent{ std::as_const(mWorld).GetComponent<BehaviorInstanceComponent>(TargetCameraEntity) };
            if (ExistingBehaviorComponent != nullptr) {
                mIsDefaultCameraControlBehaviorAttached = true;
                return;
            }

            const Script::LuaBehaviorFramework::BehaviorOperationResult AttachResult{ mLuaScriptFramework.AttachBehaviorFromFile(TargetCameraEntity, "Script/Lua/CameraController.lua") };
            if (AttachResult) {
                mIsDefaultCameraControlBehaviorAttached = true;
            }

            return;
        }
    }

    void Scene::AppendDebugWorldAxes() {
        const bool IsDrawDebugGeometriesEnabled{ (mFrameContext.RenderData.mFrameGlobals.mFlags & RenderContract::FrameGlobalFlagDrawDebugGeometry) != 0u };
        if (IsDrawDebugGeometriesEnabled == false) {
            return;
        }

        constexpr float AxisLength{ 100000.0f };
        constexpr float AxisThickness{ 0.0035f };
        const SimpleMath::Vector3 Origin{ 0.0f, 0.0f, 0.0f };
        mFrameContext.RenderData.mDebugGeometryContexts.push_back(RenderContract::DebugGeometryContext::CreateDirection(Origin, SimpleMath::Vector3{ 1.0f, 0.0f, 0.0f }, AxisLength, SimpleMath::Vector4{ 1.0f, 0.1f, 0.1f, 1.0f }, AxisThickness));
        mFrameContext.RenderData.mDebugGeometryContexts.push_back(RenderContract::DebugGeometryContext::CreateDirection(Origin, SimpleMath::Vector3{ 0.0f, 1.0f, 0.0f }, AxisLength, SimpleMath::Vector4{ 0.1f, 1.0f, 0.1f, 1.0f }, AxisThickness));
        mFrameContext.RenderData.mDebugGeometryContexts.push_back(RenderContract::DebugGeometryContext::CreateDirection(Origin, SimpleMath::Vector3{ 0.0f, 0.0f, 1.0f }, AxisLength, SimpleMath::Vector4{ 0.1f, 0.4f, 1.0f, 1.0f }, AxisThickness));
    }

    void Scene::ExecutePhase(Phase TargetPhase, float Dt) {
        switch (TargetPhase) {
            case Phase::PreUpdate:
                UpdateCameraVirtualMouseState();
                mFrameContext.MaterialGroups = &mAssetRegistry.GetMaterialGroups();
                mFrameContext.AssetRegistryResource = &mAssetRegistry;
                mFrameContext.RenderData.mModelContexts.clear();
                mFrameContext.RenderData.mBoundingBoxContexts.clear();
                mFrameContext.RenderData.mDebugGeometryContexts.clear();
                mFrameContext.RenderData.mTerrainPatchContexts.clear();
                mFrameContext.RenderData.mDrawRecords.clear();
                mFrameContext.RenderData.mBonePalette.clear();
                mFrameContext.RenderData.mEnvironmentInstanceContexts.clear();
                mFrameContext.RenderData.mEnvironmentSegmentContexts.clear();
                mFrameContext.RenderData.mEnvironmentDrawRecords.clear();
                mFrameContext.RenderData.mEnvironmentGpuDrivenFrame = RenderContract::EnvironmentGpuDrivenFrameData{};
                mFrameContext.RenderData.mEnvironmentRuntime = &mEnvironmentRuntime;
                mFrameContext.RenderData.mTerrainUploadFuture = RenderContract::Future{};
                mFrameContext.RenderData.mHasTerrainUploadFuture = false;
                mFrameContext.RenderData.mFsrFrameParameter = RenderContract::FsrFrameParameter{};
                for (RenderContract::ShadowRenderContext& ShadowRenderContext : mFrameContext.RenderData.mShadowRenderContexts) {
                    ShadowRenderContext.mModelContexts.clear();
                    ShadowRenderContext.mTerrainPatchContexts.clear();
                    ShadowRenderContext.mDrawRecords.clear();
                    ShadowRenderContext.mEnvironmentDrawRecords.clear();
                }

                mFrameContext.RenderData.mMaterials = mAssetRegistry.GetPackedMaterials();
                mFrameContext.RenderData.mFrameGlobals.mFlags = 0u;
                if (mIsBoundingBoxDrawEnabled) {
                    mFrameContext.RenderData.mFrameGlobals.mFlags |= RenderContract::FrameGlobalFlagDrawBoundingBoxes;
                }

                if (mIsDebugGeometryDrawEnabled) {
                    mFrameContext.RenderData.mFrameGlobals.mFlags |= RenderContract::FrameGlobalFlagDrawDebugGeometry;
                }

                AppendDebugWorldAxes();
                mFrameContext.RenderData.mMaterialTextureTable = mAssetRegistry.GetMaterialTextureTable();
                mFrameContext.SkinnedMeshPreparedDataItems.clear();
                break;

            case Phase::Update:
                AttachDefaultCameraControlBehavior();
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
                mFrameContext.MaterialGroups = &mAssetRegistry.GetMaterialGroups();
                mFrameContext.AssetRegistryResource = &mAssetRegistry;

                System->Execute(mWorld, mFrameContext, Dt);
            }
        }

        switch (TargetPhase) {
            case Phase::Update:
                mLuaScriptFramework.LateUpdate(Dt);
                break;

            case Phase::PostUpdate:
                UpdatePhysics(Dt);
                break;

            case Phase::PhysicsActorUpdate:
                mFrameContext.MaterialGroups = &mAssetRegistry.GetMaterialGroups();
                mFrameContext.AssetRegistryResource = &mAssetRegistry;
                mEnvironmentRuntime.Tick(mWorld, mFrameContext, Dt);
                break;

            case Phase::RenderPrepare:
                PrepareEnvironmentGpuDrivenFrame();
                break;

            default:
                break;
        }
    }
}
