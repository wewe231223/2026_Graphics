#include "Scene.h"
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "Imgui/imgui.h"
#include "Core/Config.h"
#include "Game/Base/Input.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/AnimatorGraphPlayer.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/CharacterController.h"
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/ComponentLuaTypeDefinitions.h"
#include "Game/Scene/Components/Culling.h"
#include "Game/Scene/Components/DirectionalLight.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/FootIKRig.h"
#include "Game/Scene/Components/FootIKRuntime.h"
#include "Game/Scene/Components/Frustum.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/PhysicsActor.h"
#include "Game/Scene/Components/PrefabInstance.h"
#include "Game/Scene/Components/RuntimeVariableTable.h"
#include "Game/Scene/Components/ScriptComponent.h"
#include "Game/Scene/Components/SkinnedMeshRenderer.h"
#include "Game/Scene/Components/SkySphere.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Tags.h"
#include "Game/Scene/Components/TerrainRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Base/Context.h"
#include "Game/Scene/Base/PipelineYAMLMetadata.h"
#include "Game/Scene/Base/SystemRegistry.h"
#include "RenderContract/Gather/RenderGatherResultMerger.h"
#include "RenderContract/Writer/FrameRenderWriter.h"
#include "Game/Scene/Systems/CameraRenderSystem.h"
#include "Game/Scene/Systems/CharacterControllerSystem.h"
#include "Game/Scene/Systems/PhysicsActorUpdateSystem.h"
#include "Game/Scene/Systems/ProceduralFoliageSystem.h"
#include "Game/Scene/Systems/ShadowMappingParameterSystem.h"
#include "Game/Scene/Systems/TerrainStreamingSystem.h"
#include "Utility/MathValidation.h"

namespace Game {
    namespace Pipeline {
        namespace {
            constexpr std::uint64_t InvalidWorkUnitBuildStructureVersion{ std::numeric_limits<std::uint64_t>::max() };
        }

        PipelineFrameExecutionResult::PipelineFrameExecutionResult() = default;
        PipelineFrameExecutionResult::~PipelineFrameExecutionResult() = default;
        PipelineFrameExecutionResult::PipelineFrameExecutionResult(const PipelineFrameExecutionResult& Other) = default;
        PipelineFrameExecutionResult& PipelineFrameExecutionResult::operator=(const PipelineFrameExecutionResult& Other) = default;
        PipelineFrameExecutionResult::PipelineFrameExecutionResult(PipelineFrameExecutionResult&& Other) noexcept = default;
        PipelineFrameExecutionResult& PipelineFrameExecutionResult::operator=(PipelineFrameExecutionResult&& Other) noexcept = default;

        void PipelineFrameExecutionResult::AddFailure(const std::string& Message) {
            IsSuccess = false;

            if (FailureMessage.empty() == false) {
                FailureMessage += "\n";
            }

            FailureMessage += Message;
        }

        std::string PipelineFrameExecutionResult::BuildFailureMessage(const std::string& Prefix, const std::vector<std::string>& FailureMessages) const {
            std::string Message{ Prefix };
            for (const std::string& FailureMessage : FailureMessages) {
                if (Message.empty() == false) {
                    Message += "\n";
                }

                Message += FailureMessage;
            }

            return Message;
        }

        void PipelineFrameExecutionResult::BuildFailureResult(const std::string& FailureMessage) {
            AddFailure(FailureMessage);
        }

        Scene::Scene()
            : mWorld{},
            mFrameContext{},
            mAssetRegistry{},
            mLuaScriptFramework{},
            mPhysicsWorld{},
            mPhysicsRuntime{},
            mPhysicsRuntimeScene{},
            mPhysicsRuntimeSnapshot{},
            mKinematicSceneSimulator{},
            mKinematicRuntimeStates{},
            mPhysicsSynchronizationEntityIds{},
            mPhysicsSynchronizationStructureVersion{ InvalidWorkUnitBuildStructureVersion },
            mPhysicsWorldVersion{ 1U },
            mPhysicsTime{},
            mIsPhysicsRuntimeModeEnabled{ ScenePhysicsRuntimeCoordinator::ResolvePhysicsRuntimeModeEnabled() },
            mIsDebugGeometryDrawEnabled{ ResolveInitialDebugGeometryDrawEnabled() },
            mIsBoundingBoxDrawEnabled{},
            mIsDefaultCameraControlBehaviorAttached{},
            mTerrainManager{},
            mTerrainActorDescBindings{},
            mEnvironmentRuntime{},
            mWorldSnapshot{},
            mWorldSnapshotVersion{},
            mPipelineDefinitions{},
            mUnitPipelineAssignments{},
            mSynchronousSystems{},
            mPipelineSystemsByName{},
            mWorkUnits{},
            mPipelineExecutor{},
            mWorkUnitBuildStructureVersion{ InvalidWorkUnitBuildStructureVersion },
            mWorkUnitBuildRuntimePipelineAssignmentVersion{},
            mPipelineSystemBindingStructureVersion{ InvalidWorkUnitBuildStructureVersion },
            mPipelineSystemBindingRuntimePipelineAssignmentVersion{} {
            mSynchronousSystems.push_back(std::make_unique<Game::CharacterControllerSystem>());
            mSynchronousSystems.push_back(std::make_unique<Game::PhysicsActorUpdateSystem>());
            mSynchronousSystems.push_back(std::make_unique<Game::TerrainStreamingSystem>());
            mSynchronousSystems.push_back(std::make_unique<Game::CameraRenderSystem>());
            mSynchronousSystems.push_back(std::make_unique<Game::ShadowMappingParameterSystem>());
            mSynchronousSystems.push_back(std::make_unique<Game::ProceduralFoliageSystem>());

            ScenePhysicsRuntimeCoordinator::InitializePhysicsWorld(BuildPhysicsRuntimeContext());

            mWorldSnapshot.BindReadOnlyWorld(&mWorld.GetReadOnlyView());
            mWorldSnapshot.BindWorld(&mWorld);
            mWorldSnapshot.BindAssetRegistry(&mAssetRegistry);

            mLuaScriptFramework.Initialize(&mWorld);
            mLuaScriptFramework.SetFixedUpdateInterval(1.0F);
            mLuaScriptFramework.OpenDefaultLibraries();
            RegisterScriptTypes();
        }

        Scene::~Scene() {
            ScenePhysicsRuntimeCoordinator::ShutdownPhysicsRuntime(BuildPhysicsRuntimeContext());
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

        void Scene::PrepareRender() {
            mAssetRegistry.PrepareRenderTextures(mFrameContext.RenderData);
            mFrameContext.RenderData.mMaterialTextureTable = mAssetRegistry.GetMaterialTextureTable();
            if (mEnvironmentRuntime.IsGpuDrivenEnabled() == false) {
                return;
            }

            EnvironmentFrameInput EnvironmentInput{};
            EnvironmentInput.mFrameIndex = mFrameContext.RenderData.mFrameGlobals.mFrameIndex;
            EnvironmentInput.mFocusPosition = SimpleMath::Vector3{ mFrameContext.RenderData.mMainCamera.mPosition.x, mFrameContext.RenderData.mMainCamera.mPosition.y, mFrameContext.RenderData.mMainCamera.mPosition.z };
            EnvironmentInput.mView = mFrameContext.RenderData.mFrameGlobals.mView;
            EnvironmentInput.mProjection = mFrameContext.RenderData.mFrameGlobals.mProj;
            EnvironmentInput.mViewProjection = mFrameContext.RenderData.mFrameGlobals.mViewProj;
            mEnvironmentRuntime.PrepareGpuDrivenFrame(EnvironmentInput, mFrameContext.RenderData);
        }

        PipelineFrameExecutionResult Scene::ExecuteDataPipelineFrame(float Dt, const PipelineSystemRegistry& Registry) {
            PipelineFrameExecutionResult FixedStageResult{ ExecuteDataPipelineFixedStage(Dt, Registry) };
            if (FixedStageResult.IsSuccess == false) {
                return FixedStageResult;
            }

            ExecuteDataPipelineParallelStage(Dt);
            return PipelineFrameExecutionResult{};
        }

        PipelineFrameExecutionResult Scene::ExecuteDataPipelineFixedStage(float Dt, const PipelineSystemRegistry& Registry) {
            ExecuteDataPipelineFixedStageBeforePhysics(Dt);
            ExecuteDataPipelinePhysicsStage(Dt);
            return ExecuteDataPipelineFixedStageAfterPhysics(Dt, Registry);
        }

        void Scene::ExecuteDataPipelineFixedStageBeforePhysics(float Dt) {
            InitializeDataPipelineFrameRenderData();

            AttachDefaultCameraControlBehavior();
            mLuaScriptFramework.Update(Dt);
            mLuaScriptFramework.LateUpdate(Dt);
            mFrameContext.PhysicsWorldResource = &mPhysicsWorld;
            mFrameContext.TerrainManagerResource = &mTerrainManager;
            mFrameContext.TerrainQueryResource = &mTerrainManager;
            ExecuteCharacterControllerSystems(Dt);
        }

        void Scene::ExecuteDataPipelinePhysicsStage(float Dt) {
            UpdatePhysics(Dt);
            ExecuteSynchronousSystems(Dt, true);
        }

        PipelineFrameExecutionResult Scene::ExecuteDataPipelineFixedStageAfterPhysics(float Dt, const PipelineSystemRegistry& Registry) {
            PipelineFrameExecutionResult Result{};

            ExecuteSynchronousSystems(Dt, false);
            mWorld.FlushDeferredStructuralChanges();

            SceneWorkUnitBuildResult WorkUnitBuildResult{ UpdateWorkUnitsIfNeeded() };
            if (WorkUnitBuildResult.IsSuccess == false) {
                Result.BuildFailureResult(Result.BuildFailureMessage("WorkUnit build failed.", WorkUnitBuildResult.UndecidedItems));
                return Result;
            }

            PipelineSystemBindingResult BindingResult{ BuildPipelineSystemBindings(Registry) };
            if (BindingResult.IsSuccess == false) {
                Result.BuildFailureResult(Result.BuildFailureMessage("Pipeline system binding failed.", BindingResult.FailureMessages));
                return Result;
            }

            return Result;
        }

        void Scene::ExecuteDataPipelineParallelStage(float Dt) {
            const PipelineFrameInput FrameInput{ BuildPipelineFrameInput() };
            std::span<SceneWorkUnit> WorkUnitSpan{ mWorkUnits.data(), mWorkUnits.size() };
            mPipelineExecutor.Execute(mWorld, WorkUnitSpan, FrameInput, Dt);
            RenderContract::RenderGatherResultMerger::Merge(mPipelineExecutor.GetRenderGatherResults(), mFrameContext.RenderData);
        }

        void Scene::AddSynchronousSystem(std::unique_ptr<Game::ISystem> NewSystem) {
            if (NewSystem == nullptr) {
                return;
            }

            const std::string& SystemName{ NewSystem->Name() };
            for (std::unique_ptr<Game::ISystem>& System : mSynchronousSystems) {
                if (System != nullptr && System->Name() == SystemName) {
                    System = std::move(NewSystem);
                    return;
                }
            }
        }

        AssetRegistry& Scene::GetAssetRegistry() {
            return mAssetRegistry;
        }

        const AssetRegistry& Scene::GetAssetRegistry() const {
            return mAssetRegistry;
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

        PhysicsRuntime& Scene::GetPhysicsRuntime() {
            return mPhysicsRuntime;
        }

        const PhysicsRuntime& Scene::GetPhysicsRuntime() const {
            return mPhysicsRuntime;
        }

        PhysicsRuntimeScene& Scene::GetPhysicsRuntimeScene() {
            return mPhysicsRuntimeScene;
        }

        const PhysicsRuntimeScene& Scene::GetPhysicsRuntimeScene() const {
            return mPhysicsRuntimeScene;
        }

        PhysicsSnapshot& Scene::GetPhysicsRuntimeSnapshot() {
            return mPhysicsRuntimeSnapshot;
        }

        const PhysicsSnapshot& Scene::GetPhysicsRuntimeSnapshot() const {
            return mPhysicsRuntimeSnapshot;
        }

        std::vector<PhysicsKinematicRuntimeState>& Scene::GetKinematicRuntimeStates() {
            return mKinematicRuntimeStates;
        }

        const std::vector<PhysicsKinematicRuntimeState>& Scene::GetKinematicRuntimeStates() const {
            return mKinematicRuntimeStates;
        }

        Terrain::TerrainManager& Scene::GetTerrainManager() {
            return mTerrainManager;
        }

        const Terrain::TerrainManager& Scene::GetTerrainManager() const {
            return mTerrainManager;
        }

        std::uint32_t Scene::GetPhysicsWorldVersion() const {
            return mPhysicsWorldVersion;
        }

        void Scene::SetPhysicsWorldVersion(std::uint32_t PhysicsWorldVersion) {
            mPhysicsWorldVersion = PhysicsWorldVersion;
        }

        void Scene::RebuildPhysicsActors() {
            ScenePhysicsRuntimeCoordinator::RebuildPhysicsActors(BuildPhysicsRuntimeContext());
        }

        void Scene::UpdatePhysics(float Dt) {
            ScenePhysicsRuntimeCoordinator::UpdatePhysics(BuildPhysicsRuntimeContext(), Dt);
        }

        Utility::Time* Scene::GetPhysicsTime() const {
            return mPhysicsTime;
        }

        void Scene::SetPhysicsTime(Utility::Time* PhysicsTime) {
            mPhysicsTime = PhysicsTime;
        }

        bool Scene::IsPhysicsRuntimeModeEnabled() const {
            return mIsPhysicsRuntimeModeEnabled;
        }

        void Scene::SetPhysicsRuntimeModeEnabled(bool IsPhysicsRuntimeModeEnabled) {
            mIsPhysicsRuntimeModeEnabled = IsPhysicsRuntimeModeEnabled;
            mFrameContext.IsPhysicsRuntimeModeEnabled = mIsPhysicsRuntimeModeEnabled;
        }

        void Scene::RefreshPhysicsRuntimeSnapshot() {
            ScenePhysicsRuntimeCoordinator::RefreshPhysicsRuntimeSnapshot(BuildPhysicsRuntimeContext());
        }

        void Scene::PublishPhysicsRuntimeStatus(const PhysicsSnapshot* Snapshot) {
            ScenePhysicsRuntimeCoordinator::PublishPhysicsRuntimeStatus(BuildPhysicsRuntimeContext(), Snapshot);
        }

        std::vector<TerrainActorDescBinding>& Scene::GetTerrainActorDescBindings() {
            return mTerrainActorDescBindings;
        }

        const std::vector<TerrainActorDescBinding>& Scene::GetTerrainActorDescBindings() const {
            return mTerrainActorDescBindings;
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

        SceneWorldSnapshot& Scene::GetWorldSnapshot() {
            return mWorldSnapshot;
        }

        const SceneWorldSnapshot& Scene::GetWorldSnapshot() const {
            return mWorldSnapshot;
        }

        bool Scene::AddPipelineDefinition(const PipelineDefinition& PipelineDefinitionValue) {
            if (CanAddPipelineDefinition(PipelineDefinitionValue) == false) {
                return false;
            }

            PipelineDefinition NewPipelineDefinition{ PipelineDefinitionValue };
            NewPipelineDefinition.SetPipelineId(static_cast<PipelineId>(mPipelineDefinitions.size()));
            mPipelineDefinitions.push_back(std::move(NewPipelineDefinition));
            InvalidateWorkUnits();
            return true;
        }

        bool Scene::AddPipelineDefinition(PipelineDefinition&& PipelineDefinitionValue) {
            if (CanAddPipelineDefinition(PipelineDefinitionValue) == false) {
                return false;
            }

            PipelineDefinition NewPipelineDefinition{ std::move(PipelineDefinitionValue) };
            NewPipelineDefinition.SetPipelineId(static_cast<PipelineId>(mPipelineDefinitions.size()));
            mPipelineDefinitions.push_back(std::move(NewPipelineDefinition));
            InvalidateWorkUnits();
            return true;
        }

        void Scene::ClearPipelineDefinitions() {
            mPipelineDefinitions.clear();
            InvalidateWorkUnits();
        }

        const std::vector<PipelineDefinition>& Scene::GetPipelineDefinitions() const {
            return mPipelineDefinitions;
        }

        PipelineId Scene::FindPipelineIdByName(const std::string& PipelineName) const {
            const PipelineDefinition* PipelineDefinitionValue{ FindPipelineDefinition(PipelineName) };
            if (PipelineDefinitionValue == nullptr) {
                return InvalidPipelineId;
            }

            return PipelineDefinitionValue->GetPipelineId();
        }

        PipelineDefinition* Scene::FindPipelineDefinition(const std::string& PipelineName) {
            for (PipelineDefinition& PipelineDefinitionValue : mPipelineDefinitions) {
                if (PipelineDefinitionValue.GetName() == PipelineName) {
                    return &PipelineDefinitionValue;
                }
            }

            return nullptr;
        }

        const PipelineDefinition* Scene::FindPipelineDefinition(const std::string& PipelineName) const {
            for (const PipelineDefinition& PipelineDefinitionValue : mPipelineDefinitions) {
                if (PipelineDefinitionValue.GetName() == PipelineName) {
                    return &PipelineDefinitionValue;
                }
            }

            return nullptr;
        }

        bool Scene::AddUnitPipelineAssignment(Arche::EntityID UnitEntityId, const std::string& PipelineName) {
            if (UnitEntityId == Arche::NullEntityID) {
                return false;
            }

            if (PipelineName.empty() == true) {
                return false;
            }

            if (FindUnitPipelineAssignment(UnitEntityId) != nullptr) {
                return false;
            }

            const PipelineId PipelineIdValue{ FindPipelineIdByName(PipelineName) };
            if (PipelineIdValue == InvalidPipelineId) {
                return false;
            }

            UnitPipelineAssignment NewAssignment{};
            NewAssignment.mUnitEntityId = UnitEntityId;
            NewAssignment.mPipelineId = PipelineIdValue;
            NewAssignment.mPipelineName = PipelineName;
            mUnitPipelineAssignments.push_back(std::move(NewAssignment));
            InvalidateWorkUnits();
            return true;
        }

        void Scene::ClearUnitPipelineAssignments() {
            mUnitPipelineAssignments.clear();
            InvalidateWorkUnits();
        }

        const std::vector<UnitPipelineAssignment>& Scene::GetUnitPipelineAssignments() const {
            return mUnitPipelineAssignments;
        }

        const UnitPipelineAssignment* Scene::FindUnitPipelineAssignment(Arche::EntityID UnitEntityId) const {
            for (const UnitPipelineAssignment& Assignment : mUnitPipelineAssignments) {
                if (Assignment.mUnitEntityId == UnitEntityId) {
                    return &Assignment;
                }
            }

            return nullptr;
        }

        SceneWorkUnitBuildResult Scene::ApplySerializedUnitPipelineAssignments(const std::vector<SerializedUnitPipelineAssignment>& SerializedUnitPipelineAssignments, const std::unordered_map<std::int64_t, Arche::EntityID>& EntityIdMap) {
            SceneWorkUnitBuildResult ApplyResult{};
            std::vector<UnitPipelineAssignment> OriginalUnitPipelineAssignments{ mUnitPipelineAssignments };

            for (const SerializedUnitPipelineAssignment& SerializedAssignment : SerializedUnitPipelineAssignments) {
                const std::unordered_map<std::int64_t, Arche::EntityID>::const_iterator EntityIdIter{ EntityIdMap.find(SerializedAssignment.mSerializedEntityId) };
                if (EntityIdIter == EntityIdMap.end()) {
                    ApplyResult.IsSuccess = false;
                    ApplyResult.UndecidedItems.push_back(std::string{ "Serialized EntityId is not mapped: " } + std::to_string(SerializedAssignment.mSerializedEntityId));
                    continue;
                }

                if (SerializedAssignment.mPipelineName.empty() == true) {
                    ApplyResult.IsSuccess = false;
                    ApplyResult.UndecidedItems.push_back(std::string{ "Pipeline name is empty: " } + std::to_string(SerializedAssignment.mSerializedEntityId));
                    continue;
                }

                if (FindPipelineDefinition(SerializedAssignment.mPipelineName) == nullptr) {
                    ApplyResult.IsSuccess = false;
                    ApplyResult.UndecidedItems.push_back(std::string{ "Pipeline definition is missing: " } + SerializedAssignment.mPipelineName);
                    continue;
                }

                if (FindUnitPipelineAssignment(EntityIdIter->second) != nullptr) {
                    ApplyResult.IsSuccess = false;
                    ApplyResult.UndecidedItems.push_back(std::string{ "Duplicate Unit Entity pipeline assignment: " } + std::to_string(SerializedAssignment.mSerializedEntityId));
                    continue;
                }

                if (AddUnitPipelineAssignment(EntityIdIter->second, SerializedAssignment.mPipelineName) == false) {
                    ApplyResult.IsSuccess = false;
                    ApplyResult.UndecidedItems.push_back(std::string{ "Unit Pipeline assignment could not be added: " } + std::to_string(SerializedAssignment.mSerializedEntityId));
                }
            }

            if (ApplyResult.IsSuccess == false) {
                mUnitPipelineAssignments = std::move(OriginalUnitPipelineAssignments);
                InvalidateWorkUnits();
            }

            return ApplyResult;
        }

        std::vector<SceneWorkUnit>& Scene::GetWorkUnits() {
            return mWorkUnits;
        }

        const std::vector<SceneWorkUnit>& Scene::GetWorkUnits() const {
            return mWorkUnits;
        }

        void Scene::ClearWorkUnits() {
            mWorkUnits.clear();
            InvalidateWorkUnits();
        }

        PipelineSystemBindingResult Scene::BuildPipelineSystemBindings(const PipelineSystemRegistry& Registry) {
            PipelineSystemBindingResult BindingResult{};
            if (mWorkUnitBuildStructureVersion != InvalidWorkUnitBuildStructureVersion && mPipelineSystemBindingStructureVersion == mWorkUnitBuildStructureVersion && mPipelineSystemBindingRuntimePipelineAssignmentVersion == mWorkUnitBuildRuntimePipelineAssignmentVersion) {
                return BindingResult;
            }

            std::vector<std::string> RequiredSystemNames{};
            std::unordered_set<std::string> RequiredSystemNameSet{};

            for (const PipelineDefinition& PipelineDefinitionValue : mPipelineDefinitions) {
                for (const std::string& SystemName : PipelineDefinitionValue.GetSystemNames()) {
                    if (Registry.Contains(SystemName) == false) {
                        BindingResult.AddFailure(std::string{ "Pipeline System factory is missing: " } + SystemName);
                    }

                    const bool IsRequiredSystemNameInserted{ RequiredSystemNameSet.insert(SystemName).second };
                    if (IsRequiredSystemNameInserted == true) {
                        RequiredSystemNames.push_back(SystemName);
                    }
                }
            }

            for (const SceneWorkUnit& WorkUnit : mWorkUnits) {
                const PipelineId PipelineIdValue{ WorkUnit.GetPipelineId() };
                if (PipelineIdValue == InvalidPipelineId || static_cast<std::size_t>(PipelineIdValue) >= mPipelineDefinitions.size()) {
                    BindingResult.AddFailure(std::string{ "WorkUnit PipelineId is invalid: " } + std::to_string(PipelineIdValue));
                }
            }

            if (BindingResult.IsSuccess == false) {
                return BindingResult;
            }

            std::vector<std::pair<std::string, std::unique_ptr<IPipelineSystem>>> CreatedPipelineSystems{};
            for (const std::string& SystemName : RequiredSystemNames) {
                if (mPipelineSystemsByName.find(SystemName) != mPipelineSystemsByName.end()) {
                    continue;
                }

                std::unique_ptr<IPipelineSystem> NewPipelineSystem{ Registry.CreateSystem(SystemName) };
                if (NewPipelineSystem == nullptr) {
                    BindingResult.AddFailure(std::string{ "Pipeline System factory returned null: " } + SystemName);
                    continue;
                }

                CreatedPipelineSystems.emplace_back(SystemName, std::move(NewPipelineSystem));
            }

            if (BindingResult.IsSuccess == false) {
                return BindingResult;
            }

            for (std::pair<std::string, std::unique_ptr<IPipelineSystem>>& CreatedPipelineSystem : CreatedPipelineSystems) {
                mPipelineSystemsByName.emplace(std::move(CreatedPipelineSystem.first), std::move(CreatedPipelineSystem.second));
            }

            for (SceneWorkUnit& WorkUnit : mWorkUnits) {
                const PipelineDefinition& PipelineDefinitionValue{ mPipelineDefinitions[static_cast<std::size_t>(WorkUnit.GetPipelineId())] };
                if (IsWorkUnitPipelineBindingCurrent(WorkUnit, PipelineDefinitionValue) == true) {
                    continue;
                }

                std::vector<IPipelineSystem*> PipelineSystems{};
                PipelineSystems.reserve(PipelineDefinitionValue.GetSystemNames().size());
                for (const std::string& SystemName : PipelineDefinitionValue.GetSystemNames()) {
                    IPipelineSystem* PipelineSystem{ FindPipelineSystem(SystemName) };
                    if (PipelineSystem == nullptr) {
                        BindingResult.AddFailure(std::string{ "Pipeline System instance is missing: " } + SystemName);
                        continue;
                    }

                    PipelineSystems.push_back(PipelineSystem);
                }

                WorkUnit.GetPipelineSystems() = std::move(PipelineSystems);
            }

            if (BindingResult.IsSuccess == false) {
                return BindingResult;
            }

            mPipelineSystemBindingStructureVersion = mWorkUnitBuildStructureVersion;
            mPipelineSystemBindingRuntimePipelineAssignmentVersion = mWorkUnitBuildRuntimePipelineAssignmentVersion;
            return BindingResult;
        }

        IPipelineSystem* Scene::FindPipelineSystem(const std::string& SystemName) {
            const std::unordered_map<std::string, std::unique_ptr<IPipelineSystem>>::iterator PipelineSystemIter{ mPipelineSystemsByName.find(SystemName) };
            if (PipelineSystemIter == mPipelineSystemsByName.end()) {
                return nullptr;
            }

            return PipelineSystemIter->second.get();
        }

        const IPipelineSystem* Scene::FindPipelineSystem(const std::string& SystemName) const {
            const std::unordered_map<std::string, std::unique_ptr<IPipelineSystem>>::const_iterator PipelineSystemIter{ mPipelineSystemsByName.find(SystemName) };
            if (PipelineSystemIter == mPipelineSystemsByName.end()) {
                return nullptr;
            }

            return PipelineSystemIter->second.get();
        }

        PipelineExecutor& Scene::GetPipelineExecutor() {
            return mPipelineExecutor;
        }

        const PipelineExecutor& Scene::GetPipelineExecutor() const {
            return mPipelineExecutor;
        }

        std::uint64_t Scene::GetWorkUnitBuildStructureVersion() const {
            return mWorkUnitBuildStructureVersion;
        }

        void Scene::SetWorkUnitBuildStructureVersion(std::uint64_t WorkUnitBuildStructureVersion) {
            mWorkUnitBuildStructureVersion = WorkUnitBuildStructureVersion;
            mPipelineSystemBindingStructureVersion = InvalidWorkUnitBuildStructureVersion;
        }

        SceneWorkUnitBuildResult Scene::RebuildWorkUnits() {
            std::vector<SceneWorkUnit> NewWorkUnits{};
            SceneWorkUnitBuilder Builder{};
            SceneWorkUnitBuildResult BuildResult{ Builder.Build(*this, NewWorkUnits) };
            if (BuildResult.IsSuccess == false) {
                return BuildResult;
            }

            TransferReusablePipelineBindings(std::span<const SceneWorkUnit>{ mWorkUnits.data(), mWorkUnits.size() }, NewWorkUnits);
            mWorkUnits.swap(NewWorkUnits);
            mWorkUnitBuildStructureVersion = mWorld.GetStructureVersion();
            mWorkUnitBuildRuntimePipelineAssignmentVersion = mFrameContext.mRuntimePipelineAssignmentVersion;
            return BuildResult;
        }

        SceneWorkUnitBuildResult Scene::UpdateWorkUnitsIfNeeded() {
            SceneWorkUnitBuildResult BuildResult{};
            if (mWorkUnitBuildStructureVersion == mWorld.GetStructureVersion() && mWorkUnitBuildRuntimePipelineAssignmentVersion == mFrameContext.mRuntimePipelineAssignmentVersion) {
                return BuildResult;
            }

            return RebuildWorkUnits();
        }

        ScenePhysicsRuntimeContext Scene::BuildPhysicsRuntimeContext() {
            return ScenePhysicsRuntimeContext{ 
                mWorld, 
                mPhysicsWorld, 
                mPhysicsRuntime, 
                mPhysicsRuntimeScene, 
                mPhysicsRuntimeSnapshot, 
                mKinematicSceneSimulator, 
                mTerrainManager, 
                mKinematicRuntimeStates, 
                mPhysicsSynchronizationEntityIds, 
                mPhysicsSynchronizationStructureVersion, 
                mPhysicsWorldVersion, 
                mPhysicsTime, 
                mFrameContext, 
                mWorldSnapshot, 
                mTerrainActorDescBindings, 
                mIsPhysicsRuntimeModeEnabled 
            };
        }

        PipelineFrameInput Scene::BuildPipelineFrameInput() {
            PipelineFrameInput FrameInput{};
            FrameInput.mMaterialGroups = mFrameContext.MaterialGroups;
            FrameInput.mTerrainQueryResource = mFrameContext.TerrainQueryResource;
            FrameInput.mPickedEntityId = mFrameContext.PickedEntityId;
            FrameInput.mFrameIndex = mFrameContext.RenderData.mFrameGlobals.mFrameIndex;
            FrameInput.mRenderFlags = mFrameContext.RenderData.mFrameGlobals.mFlags;
            FrameInput.mShadowMappingParameter = mFrameContext.RenderData.mShadowMappingParameter;

            for (const auto [TransformComponent, CameraComponent, FrustumComponent] : mWorld.Query<Transform, Camera, Frustum>()) {
                if (CameraComponent.isActive == false) {
                    continue;
                }

                FrameInput.mActiveCameraFrustum = FrustumComponent;
                FrameInput.mActiveCameraPosition = TransformComponent.position;
                FrameInput.mHasActiveCameraFrustum = true;
                FrameInput.mHasActiveCameraPosition = true;
                break;
            }

            return FrameInput;
        }

        bool Scene::IsWorkUnitPipelineBindingCurrent(const SceneWorkUnit& WorkUnit, const PipelineDefinition& PipelineDefinitionValue) const {
            const std::vector<std::string>& SystemNames{ PipelineDefinitionValue.GetSystemNames() };
            const std::vector<IPipelineSystem*>& PipelineSystems{ WorkUnit.GetPipelineSystems() };
            if (PipelineSystems.size() != SystemNames.size()) {
                return false;
            }

            for (std::size_t SystemIndex{}; SystemIndex < SystemNames.size(); ++SystemIndex) {
                const auto PipelineSystemIter{ mPipelineSystemsByName.find(SystemNames[SystemIndex]) };
                if (PipelineSystemIter == mPipelineSystemsByName.end() || PipelineSystems[SystemIndex] != PipelineSystemIter->second.get()) {
                    return false;
                }
            }

            return true;
        }

        void Scene::TransferReusablePipelineBindings(std::span<const SceneWorkUnit> ExistingWorkUnits, std::vector<SceneWorkUnit>& NewWorkUnits) const {
            for (SceneWorkUnit& NewWorkUnit : NewWorkUnits) {
                for (const SceneWorkUnit& ExistingWorkUnit : ExistingWorkUnits) {
                    if (NewWorkUnit.GetUnitEntityId() != ExistingWorkUnit.GetUnitEntityId() || NewWorkUnit.GetPipelineId() != ExistingWorkUnit.GetPipelineId()) {
                        continue;
                    }

                    NewWorkUnit.GetPipelineSystems() = ExistingWorkUnit.GetPipelineSystems();
                    break;
                }
            }
        }

        bool Scene::ResolveInitialDebugGeometryDrawEnabled() const {
            const IConfig* ConfigInstance{ Config::Query() };
            if (ConfigInstance == nullptr) {
                return false;
            }

            return ConfigInstance->Get<bool>("Draw_DebugGeometry");
        }

        void Scene::InitializeDataPipelineFrameRenderData() {
            UpdateCameraVirtualMouseState();
            mFrameContext.MaterialGroups = &mAssetRegistry.GetMaterialGroups();
            mFrameContext.AssetRegistryResource = &mAssetRegistry;

            RenderContract::FrameRenderWriter FrameWriter{ mFrameContext.RenderData };
            FrameWriter.BeginFrame();

            mFrameContext.RenderData.mMaterials = mAssetRegistry.GetPackedMaterials();
            if (mIsBoundingBoxDrawEnabled == true) {
                mFrameContext.RenderData.mFrameGlobals.mFlags |= RenderContract::FrameGlobalFlagDrawBoundingBoxes;
            }

            if (mIsDebugGeometryDrawEnabled == true) {
                mFrameContext.RenderData.mFrameGlobals.mFlags |= RenderContract::FrameGlobalFlagDrawDebugGeometry;
            }

            AppendDebugWorldAxes();
            mFrameContext.RenderData.mMaterialTextureTable = mAssetRegistry.GetMaterialTextureTable();
            mFrameContext.SkinnedMeshPreparedDataItems.clear();
        }

        void Scene::ExecuteCharacterControllerSystems(float Dt) {
            for (std::unique_ptr<Game::ISystem>& System : mSynchronousSystems) {
                if (System == nullptr || System->Name() != "CharacterControllerSystem") {
                    continue;
                }

                mFrameContext.MaterialGroups = &mAssetRegistry.GetMaterialGroups();
                mFrameContext.AssetRegistryResource = &mAssetRegistry;

                System->Execute(mWorld, mFrameContext, Dt);
            }
        }

        void Scene::ExecuteSynchronousSystems(float Dt, bool IsPhysicsSynchronizationStage) {
            for (std::unique_ptr<Game::ISystem>& System : mSynchronousSystems) {
                if (System == nullptr) {
                    continue;
                }

                if (System->Name() == "CharacterControllerSystem") {
                    continue;
                }

                const bool IsPhysicsActorUpdateSystem{ System->Name() == "PhysicsActorUpdateSystem" };
                if (IsPhysicsActorUpdateSystem != IsPhysicsSynchronizationStage) {
                    continue;
                }

                mFrameContext.MaterialGroups = &mAssetRegistry.GetMaterialGroups();
                mFrameContext.AssetRegistryResource = &mAssetRegistry;

                System->Execute(mWorld, mFrameContext, Dt);
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

        bool Scene::CanAddPipelineDefinition(const PipelineDefinition& PipelineDefinitionValue) const {
            if (PipelineDefinitionValue.GetName().empty() == true) {
                return false;
            }

            if (PipelineDefinitionValue.GetSystemNames().empty() == true) {
                return false;
            }

            if (mPipelineDefinitions.size() >= static_cast<std::size_t>(InvalidPipelineId)) {
                return false;
            }

            return FindPipelineDefinition(PipelineDefinitionValue.GetName()) == nullptr;
        }

        void Scene::InvalidateWorkUnits() {
            mWorkUnitBuildStructureVersion = InvalidWorkUnitBuildStructureVersion;
            mPipelineSystemBindingStructureVersion = InvalidWorkUnitBuildStructureVersion;
        }

        void Scene::RebuildWorldSnapshot() {
            mWorldSnapshot.Clear();

            std::unordered_set<std::string> AddedSystemNames{};
            for (const std::unique_ptr<Game::ISystem>& SystemInstance : mSynchronousSystems) {
                if (SystemInstance == nullptr) {
                    continue;
                }

                const std::string& SystemName{ SystemInstance->Name() };
                if (AddedSystemNames.insert(SystemName).second == true) {
                    mWorldSnapshot.AddSystemName(SystemName);
                }
            }

            for (const PipelineDefinition& PipelineDefinitionValue : mPipelineDefinitions) {
                for (const std::string& SystemName : PipelineDefinitionValue.GetSystemNames()) {
                    if (AddedSystemNames.insert(SystemName).second == true) {
                        mWorldSnapshot.AddSystemName(SystemName);
                    }
                }
            }

            for (const auto& [NameComponent, HierarchyComponent] : mWorld.Query<Name, EntityHierarchy>()) {
                if (GetNameText(NameComponent)[0] == '\0') {
                    continue;
                }

                mWorldSnapshot.AddEntity(HierarchyComponent.self, HierarchyComponent.parent);
            }

            mWorldSnapshot.BuildHierarchy();
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

        void Scene::AttachDefaultCameraControlBehavior() {
            if (mIsDefaultCameraControlBehaviorAttached == true) {
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

        void Scene::UpdateCameraVirtualMouseState() {
            Globals::Input& InputInstance{ Globals::Input::Get() };
            const bool IsDebugGeometryToggleRequested{ InputInstance.IsKeyPressed(DirectX::Keyboard::Keys::F3) };
            const bool IsThirdPersonToggleRequested{ InputInstance.IsKeyPressed(DirectX::Keyboard::Keys::F8) };
            const bool IsBoundingBoxToggleRequested{ InputInstance.IsKeyPressed(DirectX::Keyboard::Keys::F9) };
            const DirectX::Mouse::ButtonStateTracker& MouseTracker{ InputInstance.GetMouseTracker() };
            const bool IsSelectionDragInput{ mFrameContext.PickedEntityId != Arche::NullEntityID && (MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::PRESSED || MouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::HELD) };

            if (IsDebugGeometryToggleRequested == true) {
                mIsDebugGeometryDrawEnabled = mIsDebugGeometryDrawEnabled == false;
            }

            if (IsBoundingBoxToggleRequested == true) {
                mIsBoundingBoxDrawEnabled = mIsBoundingBoxDrawEnabled == false;
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

                if (IsThirdPersonToggleRequested == true) {
                    const bool IsThirdPersonMode{ (CameraComponent.cameraFlags & CameraFlagThirdPerson) != 0u };
                    if (IsThirdPersonMode == true) {
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

                if (IsSelectionDragInput == true || IsUiCapturingInput == true) {
                    return;
                }

                const bool IsThirdPersonMode{ (CameraComponent.cameraFlags & CameraFlagThirdPerson) != 0u };
                if (IsThirdPersonMode == true) {
                    InputInstance.SetRightButtonVirtualMouseEnabled(false);
                    InputInstance.SetVirtualMouse(true);
                }
                else {
                    InputInstance.SetRightButtonVirtualMouseEnabled(true);
                }

                return;
            }
        }
    }
}
