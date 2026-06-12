#pragma once
#include <cstdint>
#include <vector>
#include "Arche/Common.h"
#include "PhysicsLib/Runtime/PhysicsRuntimeTypes.h"
#include "PhysicsLib/Actors/PhysicsTerrainActor.h"
#include "PhysicsLib/World/PhysicsWorld.h"

class PhysicsKinematicSceneSimulator;
class PhysicsRuntime;
struct PhysicsSnapshot;

namespace Arche {
    class World;
}

namespace Utility {
    class Time;
}

namespace Game {
    class SceneWorldSnapshot;
    class TerrainManager;
    struct FrameContext;
    struct TerrainActorDescBinding;

    struct ScenePhysicsRuntimeContext final {
    public:
        Arche::World& mWorld;
        PhysicsWorld& mPhysicsWorld;
        PhysicsRuntime& mPhysicsRuntime;
        PhysicsRuntimeScene& mPhysicsRuntimeScene;
        PhysicsSnapshot& mPhysicsRuntimeSnapshot;
        PhysicsKinematicSceneSimulator& mKinematicSceneSimulator;
        TerrainManager& mTerrainManager;
        std::vector<PhysicsKinematicRuntimeState>& mKinematicRuntimeStates;
        std::uint32_t& mPhysicsWorldVersion;
        Utility::Time* mPhysicsTime{};
        FrameContext& mFrameContext;
        SceneWorldSnapshot& mWorldSnapshot;
        std::vector<TerrainActorDescBinding>& mTerrainActorDescBindings;
        bool& mIsPhysicsRuntimeModeEnabled;
    };

    class ScenePhysicsRuntimeCoordinator final {
    public:
        static PhysicsWorld::WorldSettings BuildDefaultPhysicsWorldSettings();
        static bool ResolvePhysicsRuntimeModeEnabled();
        static double ResolveRenderPhysicsDelaySeconds();

        static void InitializePhysicsWorld(ScenePhysicsRuntimeContext Context);
        static void InitializePhysicsRuntime(ScenePhysicsRuntimeContext Context);
        static void ShutdownPhysicsRuntime(ScenePhysicsRuntimeContext Context);
        static void SubmitPhysicsRuntimeCommands(ScenePhysicsRuntimeContext Context);
        static void PublishPhysicsRuntimeKinematicStates(ScenePhysicsRuntimeContext Context);
        static void RefreshPhysicsRuntimeSnapshot(ScenePhysicsRuntimeContext Context);
        static void PublishPhysicsRuntimeStatus(ScenePhysicsRuntimeContext Context, const PhysicsSnapshot* Snapshot);
        static void UpdateTerrainManager(ScenePhysicsRuntimeContext Context);
        static void UpdateSceneKinematicActors(ScenePhysicsRuntimeContext Context, float Dt);
        static void RebuildPhysicsActors(ScenePhysicsRuntimeContext Context);
        static void UpdatePhysics(ScenePhysicsRuntimeContext Context, float Dt);
        static void AddTerrainActorDesc(ScenePhysicsRuntimeContext Context, Arche::EntityID EntityId, const PhysicsTerrainActor::ActorDesc& TerrainActorDesc);
        static void ClearTerrainActorDescs(ScenePhysicsRuntimeContext Context);
    };
}
