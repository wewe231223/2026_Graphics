#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Arche/World.h"
#include "Game/Model/AssetRegistry.h"
#include "Game/Scene/FrameContext.h"
#include "Game/Scene/Pipeline/PipelineExecutor.h"
#include "Game/Scene/Pipeline/PipelineSystemBindingResult.h"
#include "Game/Scene/Pipeline/PipelineTypes.h"
#include "Game/Scene/Pipeline/SceneWorkUnit.h"
#include "Game/Scene/Pipeline/SceneWorkUnitBuilder.h"
#include "Game/Scene/ScenePhysicsRuntimeCoordinator.h"
#include "Game/Scene/SceneTerrainBindings.h"
#include "Game/Scene/SceneWorldSnapshot.h"
#include "Game/Terrain/TerrainManager.h"
#include "PhysicsLib/Runtime/PhysicsRuntime.h"
#include "PhysicsLib/Runtime/PhysicsRuntimeTypes.h"
#include "PhysicsLib/Simulation/Kinematic/PhysicsKinematicSceneSimulator.h"
#include "PhysicsLib/World/PhysicsWorld.h"
#include "Script/Core/LuaScriptFramework.h"

namespace Utility {
    class Time;
}

namespace Game {
    namespace Pipeline {
        class PipelineSystemRegistry;
        struct SerializedUnitPipelineAssignment;

        struct PipelineFrameExecutionResult final {
        public:
            PipelineFrameExecutionResult();
            ~PipelineFrameExecutionResult();

            PipelineFrameExecutionResult(const PipelineFrameExecutionResult& Other);
            PipelineFrameExecutionResult& operator=(const PipelineFrameExecutionResult& Other);

            PipelineFrameExecutionResult(PipelineFrameExecutionResult&& Other) noexcept;
            PipelineFrameExecutionResult& operator=(PipelineFrameExecutionResult&& Other) noexcept;

        public:
            bool IsSuccess{ true };
            std::string FailureMessage{};
        };

        struct UnitPipelineAssignment final {
            Arche::EntityID mUnitEntityId{ Arche::NullEntityID };
            PipelineId mPipelineId{ InvalidPipelineId };
            std::string mPipelineName{};
        };

        class Scene final {
        public:
            Scene();
            ~Scene();

            Scene(const Scene& Other) = delete;
            Scene& operator=(const Scene& Other) = delete;

            Scene(Scene&& Other) noexcept = delete;
            Scene& operator=(Scene&& Other) noexcept = delete;

        public:
            Arche::World& GetWorld();
            const Arche::World& GetWorld() const;

            FrameContext& GetFrameContext();
            const FrameContext& GetFrameContext() const;

            RFD::RenderFrameData& GetRenderFrameData();
            const RFD::RenderFrameData& GetRenderFrameData() const;
            void SetRenderFrameIndex(std::uint32_t RenderFrameIndex);
            PipelineFrameExecutionResult ExecuteDataPipelineFrame(float Dt, const PipelineSystemRegistry& Registry);

            AssetRegistry& GetAssetRegistry();
            const AssetRegistry& GetAssetRegistry() const;

            Script::LuaBehaviorFramework& GetLuaScriptFramework();
            const Script::LuaBehaviorFramework& GetLuaScriptFramework() const;

            PhysicsWorld& GetPhysicsWorld();
            const PhysicsWorld& GetPhysicsWorld() const;

            PhysicsRuntime& GetPhysicsRuntime();
            const PhysicsRuntime& GetPhysicsRuntime() const;

            PhysicsRuntimeScene& GetPhysicsRuntimeScene();
            const PhysicsRuntimeScene& GetPhysicsRuntimeScene() const;

            PhysicsSnapshot& GetPhysicsRuntimeSnapshot();
            const PhysicsSnapshot& GetPhysicsRuntimeSnapshot() const;

            PhysicsKinematicSceneSimulator& GetKinematicSceneSimulator();
            const PhysicsKinematicSceneSimulator& GetKinematicSceneSimulator() const;

            std::vector<PhysicsKinematicRuntimeState>& GetKinematicRuntimeStates();
            const std::vector<PhysicsKinematicRuntimeState>& GetKinematicRuntimeStates() const;

            TerrainManager& GetTerrainManager();
            const TerrainManager& GetTerrainManager() const;

            std::uint32_t GetPhysicsWorldVersion() const;
            void SetPhysicsWorldVersion(std::uint32_t PhysicsWorldVersion);
            void RebuildPhysicsActors();

            Utility::Time* GetPhysicsTime() const;
            void SetPhysicsTime(Utility::Time* PhysicsTime);

            bool IsPhysicsRuntimeModeEnabled() const;
            void SetPhysicsRuntimeModeEnabled(bool IsPhysicsRuntimeModeEnabled);
            void RefreshPhysicsRuntimeSnapshot();
            void PublishPhysicsRuntimeStatus(const PhysicsSnapshot* Snapshot);

            std::vector<TerrainActorDescBinding>& GetTerrainActorDescBindings();
            const std::vector<TerrainActorDescBinding>& GetTerrainActorDescBindings() const;
            void AddTerrainActorDesc(Arche::EntityID EntityId, const PhysicsTerrainActor::ActorDesc& TerrainActorDesc);
            void ClearTerrainActorDescs();

            SceneWorldSnapshot& GetWorldSnapshot();
            const SceneWorldSnapshot& GetWorldSnapshot() const;

            bool AddPipelineDefinition(const PipelineDefinition& PipelineDefinitionValue);
            bool AddPipelineDefinition(PipelineDefinition&& PipelineDefinitionValue);
            void ClearPipelineDefinitions();
            const std::vector<PipelineDefinition>& GetPipelineDefinitions() const;
            PipelineId FindPipelineIdByName(const std::string& PipelineName) const;
            PipelineDefinition* FindPipelineDefinition(const std::string& PipelineName);
            const PipelineDefinition* FindPipelineDefinition(const std::string& PipelineName) const;

            bool AddUnitPipelineAssignment(Arche::EntityID UnitEntityId, const std::string& PipelineName);
            void ClearUnitPipelineAssignments();
            const std::vector<UnitPipelineAssignment>& GetUnitPipelineAssignments() const;
            const UnitPipelineAssignment* FindUnitPipelineAssignment(Arche::EntityID UnitEntityId) const;
            SceneWorkUnitBuildResult ApplySerializedUnitPipelineAssignments(const std::vector<SerializedUnitPipelineAssignment>& SerializedUnitPipelineAssignments, const std::unordered_map<std::int64_t, Arche::EntityID>& EntityIdMap);

            std::vector<SceneWorkUnit>& GetWorkUnits();
            const std::vector<SceneWorkUnit>& GetWorkUnits() const;
            void ClearWorkUnits();

            PipelineSystemBindingResult BuildPipelineSystemBindings(const PipelineSystemRegistry& Registry);
            IPipelineSystem* FindPipelineSystem(const std::string& SystemName);
            const IPipelineSystem* FindPipelineSystem(const std::string& SystemName) const;

            PipelineExecutor& GetPipelineExecutor();
            const PipelineExecutor& GetPipelineExecutor() const;

            std::uint64_t GetWorkUnitBuildStructureVersion() const;
            void SetWorkUnitBuildStructureVersion(std::uint64_t WorkUnitBuildStructureVersion);
            SceneWorkUnitBuildResult RebuildWorkUnits();
            SceneWorkUnitBuildResult UpdateWorkUnitsIfNeeded();

        private:
            ScenePhysicsRuntimeContext BuildPhysicsRuntimeContext();
            void InitializeDataPipelineFrameRenderData();
            bool CanAddPipelineDefinition(const PipelineDefinition& PipelineDefinitionValue) const;
            void InvalidateWorkUnits();

        private:
            Arche::World mWorld{};
            FrameContext mFrameContext{};
            AssetRegistry mAssetRegistry{};
            Script::LuaBehaviorFramework mLuaScriptFramework{};

            PhysicsWorld mPhysicsWorld{};
            PhysicsRuntime mPhysicsRuntime{};
            PhysicsRuntimeScene mPhysicsRuntimeScene{};
            PhysicsSnapshot mPhysicsRuntimeSnapshot{};
            PhysicsKinematicSceneSimulator mKinematicSceneSimulator{};
            std::vector<PhysicsKinematicRuntimeState> mKinematicRuntimeStates{};
            std::uint32_t mPhysicsWorldVersion{ 1U };
            Utility::Time* mPhysicsTime{};
            bool mIsPhysicsRuntimeModeEnabled{};

            TerrainManager mTerrainManager{};
            std::vector<TerrainActorDescBinding> mTerrainActorDescBindings{};

            SceneWorldSnapshot mWorldSnapshot{};
            std::vector<PipelineDefinition> mPipelineDefinitions{};
            std::vector<UnitPipelineAssignment> mUnitPipelineAssignments{};
            std::unordered_map<std::string, std::unique_ptr<IPipelineSystem>> mPipelineSystemsByName{};
            std::vector<SceneWorkUnit> mWorkUnits{};
            PipelineExecutor mPipelineExecutor{};
            std::uint64_t mWorkUnitBuildStructureVersion{};
        };
    }
}
