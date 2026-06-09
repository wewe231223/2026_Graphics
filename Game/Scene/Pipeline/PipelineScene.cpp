#include "PipelineScene.h"
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "Game/Scene/Pipeline/PipelineSceneYamlMetadata.h"
#include "Game/Scene/Pipeline/PipelineSystemRegistry.h"
#include "Game/Scene/Pipeline/RenderGatherResultMerger.h"

namespace Game {
    namespace Pipeline {
        namespace {
            constexpr std::uint64_t InvalidWorkUnitBuildStructureVersion{ std::numeric_limits<std::uint64_t>::max() };

            void AddFailure(PipelineSystemBindingResult& BindingResult, const std::string& Message);
            IPipelineSystem* FindCreatedPipelineSystem(const std::vector<std::pair<std::string, std::unique_ptr<IPipelineSystem>>>& CreatedPipelineSystems, const std::string& SystemName);
            std::string BuildFailureMessage(const std::string& Prefix, const std::vector<std::string>& FailureMessages);
            PipelineFrameExecutionResult BuildFailureResult(const std::string& FailureMessage);

            void AddFailure(PipelineSystemBindingResult& BindingResult, const std::string& Message) {
                BindingResult.IsSuccess = false;
                BindingResult.FailureMessages.push_back(Message);
            }

            IPipelineSystem* FindCreatedPipelineSystem(const std::vector<std::pair<std::string, std::unique_ptr<IPipelineSystem>>>& CreatedPipelineSystems, const std::string& SystemName) {
                for (const std::pair<std::string, std::unique_ptr<IPipelineSystem>>& CreatedPipelineSystem : CreatedPipelineSystems) {
                    if (CreatedPipelineSystem.first == SystemName) {
                        return CreatedPipelineSystem.second.get();
                    }
                }

                return nullptr;
            }

            std::string BuildFailureMessage(const std::string& Prefix, const std::vector<std::string>& FailureMessages) {
                std::string FailureMessage{ Prefix };
                for (const std::string& Message : FailureMessages) {
                    if (FailureMessage.empty() == false) {
                        FailureMessage += "\n";
                    }

                    FailureMessage += Message;
                }

                return FailureMessage;
            }

            PipelineFrameExecutionResult BuildFailureResult(const std::string& FailureMessage) {
                PipelineFrameExecutionResult Result{};
                Result.IsSuccess = false;
                Result.FailureMessage = FailureMessage;
                return Result;
            }
        }

        PipelineFrameExecutionResult::PipelineFrameExecutionResult() = default;
        PipelineFrameExecutionResult::~PipelineFrameExecutionResult() = default;
        PipelineFrameExecutionResult::PipelineFrameExecutionResult(const PipelineFrameExecutionResult& Other) = default;
        PipelineFrameExecutionResult& PipelineFrameExecutionResult::operator=(const PipelineFrameExecutionResult& Other) = default;
        PipelineFrameExecutionResult::PipelineFrameExecutionResult(PipelineFrameExecutionResult&& Other) noexcept = default;
        PipelineFrameExecutionResult& PipelineFrameExecutionResult::operator=(PipelineFrameExecutionResult&& Other) noexcept = default;

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
            mPhysicsWorldVersion{ 1U },
            mPhysicsTime{},
            mIsPhysicsRuntimeModeEnabled{ ScenePhysicsRuntimeCoordinator::ResolvePhysicsRuntimeModeEnabled() },
            mTerrainManager{},
            mTerrainActorDescBindings{},
            mWorldSnapshot{},
            mPipelineDefinitions{},
            mUnitPipelineAssignments{},
            mPipelineSystemsByName{},
            mWorkUnits{},
            mPipelineExecutor{},
            mWorkUnitBuildStructureVersion{ InvalidWorkUnitBuildStructureVersion } {
            ScenePhysicsRuntimeCoordinator::InitializePhysicsWorld(BuildPhysicsRuntimeContext());

            mWorldSnapshot.BindReadOnlyWorld(&mWorld.GetReadOnlyView());
            mWorldSnapshot.BindWorld(&mWorld);
            mWorldSnapshot.BindAssetRegistry(&mAssetRegistry);

            mLuaScriptFramework.Initialize(&mWorld);
            mLuaScriptFramework.SetFixedUpdateInterval(1.0F);
            mLuaScriptFramework.OpenDefaultLibraries();
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

        RFD::RenderFrameData& Scene::GetRenderFrameData() {
            return mFrameContext.RenderData;
        }

        const RFD::RenderFrameData& Scene::GetRenderFrameData() const {
            return mFrameContext.RenderData;
        }

        void Scene::SetRenderFrameIndex(std::uint32_t RenderFrameIndex) {
            mFrameContext.RenderData.globals.frameIndex = RenderFrameIndex;
        }

        PipelineFrameExecutionResult Scene::ExecuteDataPipelineFrame(float Dt, const PipelineSystemRegistry& Registry) {
            InitializeDataPipelineFrameRenderData();

            mLuaScriptFramework.Update(Dt);
            mLuaScriptFramework.LateUpdate(Dt);
            RefreshPhysicsRuntimeSnapshot();
            mWorld.FlushDeferredStructuralChanges();

            SceneWorkUnitBuildResult WorkUnitBuildResult{ UpdateWorkUnitsIfNeeded() };
            if (WorkUnitBuildResult.IsSuccess == false) {
                return BuildFailureResult(BuildFailureMessage("WorkUnit build failed.", WorkUnitBuildResult.UndecidedItems));
            }

            PipelineSystemBindingResult BindingResult{ BuildPipelineSystemBindings(Registry) };
            if (BindingResult.IsSuccess == false) {
                return BuildFailureResult(BuildFailureMessage("Pipeline system binding failed.", BindingResult.FailureMessages));
            }

            std::span<SceneWorkUnit> WorkUnitSpan{ mWorkUnits.data(), mWorkUnits.size() };
            mPipelineExecutor.Execute(mWorld, WorkUnitSpan, Dt);
            RenderGatherResultMerger::Merge(std::span<const SceneWorkUnit>{ mWorkUnits.data(), mWorkUnits.size() }, mFrameContext.RenderData);
            return PipelineFrameExecutionResult{};
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

        PhysicsKinematicSceneSimulator& Scene::GetKinematicSceneSimulator() {
            return mKinematicSceneSimulator;
        }

        const PhysicsKinematicSceneSimulator& Scene::GetKinematicSceneSimulator() const {
            return mKinematicSceneSimulator;
        }

        std::vector<PhysicsKinematicRuntimeState>& Scene::GetKinematicRuntimeStates() {
            return mKinematicRuntimeStates;
        }

        const std::vector<PhysicsKinematicRuntimeState>& Scene::GetKinematicRuntimeStates() const {
            return mKinematicRuntimeStates;
        }

        TerrainManager& Scene::GetTerrainManager() {
            return mTerrainManager;
        }

        const TerrainManager& Scene::GetTerrainManager() const {
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
            std::vector<std::string> RequiredSystemNames{};
            std::unordered_set<std::string> RequiredSystemNameSet{};

            for (const PipelineDefinition& PipelineDefinitionValue : mPipelineDefinitions) {
                for (const std::string& SystemName : PipelineDefinitionValue.GetSystemNames()) {
                    if (Registry.Contains(SystemName) == false) {
                        AddFailure(BindingResult, std::string{ "Pipeline System factory is missing: " } + SystemName);
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
                    AddFailure(BindingResult, std::string{ "WorkUnit PipelineId is invalid: " } + std::to_string(PipelineIdValue));
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
                    AddFailure(BindingResult, std::string{ "Pipeline System factory returned null: " } + SystemName);
                    continue;
                }

                CreatedPipelineSystems.emplace_back(SystemName, std::move(NewPipelineSystem));
            }

            if (BindingResult.IsSuccess == false) {
                return BindingResult;
            }

            std::vector<std::vector<IPipelineSystem*>> WorkUnitPipelineSystems{};
            WorkUnitPipelineSystems.reserve(mWorkUnits.size());
            for (const SceneWorkUnit& WorkUnit : mWorkUnits) {
                const PipelineDefinition& PipelineDefinitionValue{ mPipelineDefinitions[static_cast<std::size_t>(WorkUnit.GetPipelineId())] };
                std::vector<IPipelineSystem*> PipelineSystems{};
                PipelineSystems.reserve(PipelineDefinitionValue.GetSystemNames().size());

                for (const std::string& SystemName : PipelineDefinitionValue.GetSystemNames()) {
                    IPipelineSystem* PipelineSystem{ FindPipelineSystem(SystemName) };
                    if (PipelineSystem == nullptr) {
                        PipelineSystem = FindCreatedPipelineSystem(CreatedPipelineSystems, SystemName);
                    }

                    if (PipelineSystem == nullptr) {
                        AddFailure(BindingResult, std::string{ "Pipeline System instance is missing: " } + SystemName);
                        continue;
                    }

                    PipelineSystems.push_back(PipelineSystem);
                }

                WorkUnitPipelineSystems.push_back(std::move(PipelineSystems));
            }

            if (BindingResult.IsSuccess == false) {
                return BindingResult;
            }

            for (std::pair<std::string, std::unique_ptr<IPipelineSystem>>& CreatedPipelineSystem : CreatedPipelineSystems) {
                mPipelineSystemsByName.emplace(std::move(CreatedPipelineSystem.first), std::move(CreatedPipelineSystem.second));
            }

            for (std::size_t WorkUnitIndex{}; WorkUnitIndex < mWorkUnits.size(); ++WorkUnitIndex) {
                mWorkUnits[WorkUnitIndex].GetPipelineSystems() = std::move(WorkUnitPipelineSystems[WorkUnitIndex]);
            }

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
        }

        SceneWorkUnitBuildResult Scene::RebuildWorkUnits() {
            std::vector<SceneWorkUnit> NewWorkUnits{};
            SceneWorkUnitBuilder Builder{};
            SceneWorkUnitBuildResult BuildResult{ Builder.Build(*this, NewWorkUnits) };
            if (BuildResult.IsSuccess == false) {
                return BuildResult;
            }

            mWorkUnits.swap(NewWorkUnits);
            mWorkUnitBuildStructureVersion = mWorld.GetStructureVersion();
            return BuildResult;
        }

        SceneWorkUnitBuildResult Scene::UpdateWorkUnitsIfNeeded() {
            SceneWorkUnitBuildResult BuildResult{};
            if (mWorkUnitBuildStructureVersion == mWorld.GetStructureVersion()) {
                return BuildResult;
            }

            return RebuildWorkUnits();
        }

        ScenePhysicsRuntimeContext Scene::BuildPhysicsRuntimeContext() {
            return ScenePhysicsRuntimeContext{ mWorld, mPhysicsWorld, mPhysicsRuntime, mPhysicsRuntimeScene, mPhysicsRuntimeSnapshot, mKinematicSceneSimulator, mTerrainManager, mKinematicRuntimeStates, mPhysicsWorldVersion, mPhysicsTime, mFrameContext, mWorldSnapshot, mTerrainActorDescBindings, mIsPhysicsRuntimeModeEnabled };
        }

        void Scene::InitializeDataPipelineFrameRenderData() {
            mFrameContext.RenderData.modelContexts.clear();
            mFrameContext.RenderData.boundingBoxContexts.clear();
            mFrameContext.RenderData.debugGeometryContexts.clear();
            mFrameContext.RenderData.TerrainPatchContexts.clear();
            mFrameContext.RenderData.drawRecords.clear();
            mFrameContext.RenderData.bonePalette.clear();
            mFrameContext.RenderData.mTerrainUploadFutures.clear();

            for (RFD::ShadowRenderContext& ShadowRenderContext : mFrameContext.RenderData.ShadowRenderContexts) {
                ShadowRenderContext.ModelContexts.clear();
                ShadowRenderContext.TerrainPatchContexts.clear();
                ShadowRenderContext.DrawRecords.clear();
            }

            mFrameContext.RenderData.materials = mAssetRegistry.GetPackedMaterials();
            mFrameContext.RenderData.materialTextureTable = mAssetRegistry.GetMaterialTextureTable();
            mFrameContext.SkinnedMeshPreparedDataItems.clear();
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
        }
    }
}
