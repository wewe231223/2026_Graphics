#include "PipelineScene.h"
#include <limits>
#include <utility>
#include "Game/Scene/Pipeline/PipelineSceneYamlMetadata.h"

namespace Game {
    namespace Pipeline {
        namespace {
            constexpr std::uint64_t InvalidWorkUnitBuildStructureVersion{ std::numeric_limits<std::uint64_t>::max() };
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
            mPhysicsWorldVersion{ 1U },
            mPhysicsTime{},
            mIsPhysicsRuntimeModeEnabled{ ScenePhysicsRuntimeCoordinator::ResolvePhysicsRuntimeModeEnabled() },
            mTerrainManager{},
            mTerrainActorDescBindings{},
            mWorldSnapshot{},
            mPipelineDefinitions{},
            mUnitPipelineAssignments{},
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
