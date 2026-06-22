#pragma once
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "Arche/World.h"
#include "Game/Scene/Base/SynchronousSystem.h"
#include "SystemSceduler.h"
#include "Game/Scene/Base/SceneWorldSnapshot.h"
#include "Game/Model/AssetRegistry.h"
#include "Script/Core/LuaScriptFramework.h"
#include "PhysicsLib/Runtime/PhysicsRuntime.h"
#include "PhysicsLib/Runtime/PhysicsRuntimeTypes.h"
#include "PhysicsLib/Simulation/Kinematic/PhysicsKinematicSceneSimulator.h"
#include "PhysicsLib/World/PhysicsWorld.h"
#include "Game/Scene/Base/SceneTerrainBindings.h"
#include "Terrain/TerrainManager.h"

namespace Utility {
    class Time;
}

namespace Game {
    struct ScenePhysicsRuntimeContext;

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

        RenderContract::RenderFrameData& GetRenderFrameData();
        const RenderContract::RenderFrameData& GetRenderFrameData() const;
        void SetRenderFrameIndex(std::uint32_t RenderFrameIndex);

        AssetRegistry& GetAssetRegistry();
        const AssetRegistry& GetAssetRegistry() const;
        Script::LuaBehaviorFramework& GetLuaScriptFramework();
        const Script::LuaBehaviorFramework& GetLuaScriptFramework() const;
        PhysicsWorld& GetPhysicsWorld();
        const PhysicsWorld& GetPhysicsWorld() const;
        const PhysicsRuntime& GetPhysicsRuntime() const;
        const PhysicsRuntimeScene& GetPhysicsRuntimeScene() const;
        std::uint32_t GetPhysicsWorldVersion() const;
        bool IsPhysicsRuntimeModeEnabled() const;

        void InitializeAssetRegistry(ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Core::DX::DescriptorHeap* SrvHeap);
        void InitializePhysicsWorld();
        void RebuildPhysicsActors();
        void UpdatePhysics(float Dt);
        void SetPhysicsTime(Utility::Time* PhysicsTime);
        void SetName(const std::string& NewName);
        const std::string& GetName() const;

        void AddSystem(std::unique_ptr<ISystem> NewSystem);
        void BuildSystemExecutionPlan();
        void ExecutePhase(Phase TargetPhase, float Dt);
        void PrepareRender();

        void InitializeWorldSnapshot();
        void AddTerrainActorDesc(Arche::EntityID EntityId, const PhysicsTerrainActor::ActorDesc& TerrainActorDesc);
        void ClearTerrainActorDescs();

        void OnFileDropped(const std::filesystem::path& FilePath);
        void UpdateWorldSnapshotIfNeeded();
        const SceneWorldSnapshot& GetWorldSnapshot() const;


    private:
        ScenePhysicsRuntimeContext BuildPhysicsRuntimeContext();
        void RebuildWorldSnapshot();
        void SpawnModelAtOrigin(const std::string& ModelSelector, const std::string& RootEntityName, std::uint32_t MaterialGroupIndex, bool IsDerivedEntity);

        void InitializePhysicsRuntime();
        void ShutdownPhysicsRuntime();
        void SubmitPhysicsRuntimeCommands();
        void PublishPhysicsRuntimeKinematicStates();
        void RefreshPhysicsRuntimeSnapshot();
        void PublishPhysicsRuntimeStatus(const PhysicsSnapshot* Snapshot);
        void UpdateTerrainManager();
        void UpdateSceneKinematicActors(float Dt);

        void RegisterScriptTypes(); 
        void AttachDefaultCameraControlBehavior();
        void UpdateCameraVirtualMouseState();
        void AppendDebugWorldAxes();
    private:
        std::string mName{};
        Arche::World mWorld{};
        PhysicsWorld mPhysicsWorld{};
        PhysicsRuntime mPhysicsRuntime{};
        
        PhysicsRuntimeScene mPhysicsRuntimeScene{};
        PhysicsSnapshot mPhysicsRuntimeSnapshot{};
        PhysicsKinematicSceneSimulator mKinematicSceneSimulator{};
        std::vector<PhysicsKinematicRuntimeState> mKinematicRuntimeStates{};
        std::vector<Arche::EntityID> mPhysicsSynchronizationEntityIds{};
        std::uint64_t mPhysicsSynchronizationStructureVersion{ std::numeric_limits<std::uint64_t>::max() };
        std::uint32_t mPhysicsWorldVersion{ 1U };
        Utility::Time* mPhysicsTime{};
        
        Terrain::TerrainManager mTerrainManager{};
        
        FrameContext mFrameContext{};
        AssetRegistry mAssetRegistry{};
        std::vector<std::unique_ptr<ISystem>> mSystems{};
        SystemSceduler mSystemSceduler{};

        SceneWorldSnapshot mWorldSnapshot{};
        std::uint64_t mWorldSnapshotVersion{};
        std::uint64_t mHierarchyEntitySelectedSubscriptionId{};
        std::uint64_t mFileDropSubscriptionId{};

        bool mIsDefaultCameraControlBehaviorAttached{};
        bool mIsDebugGeometryDrawEnabled{};
        bool mIsBoundingBoxDrawEnabled{};
        bool mIsPhysicsRuntimeModeEnabled{};

		Script::LuaBehaviorFramework mLuaScriptFramework{};
        std::vector<TerrainActorDescBinding> mTerrainActorDescBindings{};
    };
}
