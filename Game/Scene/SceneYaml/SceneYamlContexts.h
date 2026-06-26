#pragma once

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Arche/World.h"
#include "Game/Model/AssetRegistry.h"
#include "Game/Scene/Legacy/Scene.h"
#include "Game/Scene/Base/SceneEntityFactory.h"
#include "Game/Scene/Base/SceneWorldSnapshot.h"
#include "SceneYamlTypes.h"

namespace Game {
    namespace Pipeline {
        class Scene;
    }
}

namespace Game::SceneYaml {
    class SceneYamlLoadTarget final {
    public:
        SceneYamlLoadTarget(Scene& TargetScene);
        SceneYamlLoadTarget(Pipeline::Scene& TargetScene);
        ~SceneYamlLoadTarget();
        SceneYamlLoadTarget(const SceneYamlLoadTarget& Other);
        SceneYamlLoadTarget& operator=(const SceneYamlLoadTarget& Other);
        SceneYamlLoadTarget(SceneYamlLoadTarget&& Other) noexcept;
        SceneYamlLoadTarget& operator=(SceneYamlLoadTarget&& Other) noexcept;

    public:
        Arche::World& GetWorld();
        const Arche::World& GetWorld() const;

        AssetRegistry& GetAssetRegistry();
        const AssetRegistry& GetAssetRegistry() const;

        Script::LuaBehaviorFramework& GetLuaScriptFramework();
        const Script::LuaBehaviorFramework& GetLuaScriptFramework() const;

        void SetName(const std::string& NewName);
        void SetEnvironmentConfigPath(const std::string& ConfigPath);
        bool ShouldReadSystems() const;
        void AddSystem(std::unique_ptr<ISystem> NewSystem);
        void BuildSystemExecutionPlan();
        void RebuildPhysicsActors();
        void AddTerrainActorDesc(Arche::EntityID EntityId, const PhysicsTerrainActor::ActorDesc& TerrainActorDesc);
        void ClearTerrainActorDescs();

    private:
        Scene* mScene{};
        Pipeline::Scene* mPipelineScene{};
    };

    struct SceneYamlLoadContext final {
    public:
        SceneYamlLoadContext(Scene& TargetScene, SceneYamlLoadResult& TargetLoadResult);
        SceneYamlLoadContext(Pipeline::Scene& TargetScene, SceneYamlLoadResult& TargetLoadResult);

    public:
        SceneYamlLoadTarget mScene;
        SceneYamlLoadResult& mLoadResult;
        SceneEntityFactory mEntityFactory;
        std::string mSceneName{};
        std::unordered_map<std::uint64_t, PrefabDescriptor> mPrefabDescriptors{};
        std::unordered_map<std::int64_t, Arche::EntityID> mEntityBySerializedId{};
        std::vector<std::pair<Arche::EntityID, std::int64_t>> mDeferredParents{};
        std::vector<std::pair<Arche::EntityID, std::int64_t>> mDeferredBoneSkinReferenceEntities{};
        std::vector<std::pair<Arche::EntityID, std::int64_t>> mDeferredThirdPersonFollowTargetEntities{};
        std::vector<std::pair<Arche::EntityID, std::int64_t>> mDeferredSkySphereEntities{};
        std::vector<PendingAnimatorBinding> mPendingAnimatorBindings{};
        std::vector<PendingBoundingBoxBinding> mPendingBoundingBoxBindings{};
        std::vector<PendingTerrainSnapBinding> mPendingTerrainSnapBindings{};
        std::vector<TerrainSurfaceBinding> mTerrainSurfaceBindings{};
    };

    struct SceneYamlComponentReadState final {
    public:
        std::uint32_t mMaterialGroupIndexForModel{};
        bool mHasPrefabInstance{};
        std::string mPrefabModelSelector{};
        bool mPrefabIsActive{ true };
        bool mHasInstantiatedPrefabModel{};
        bool mFrustumCullingEnabled{ true };
        bool mShouldStopReadingEntity{};
    };

    struct SceneYamlComponentWriteContext final {
    public:
        std::ostringstream& mStream;
        SceneYamlSaveResult& mSaveResult;
        const SceneWorldSnapshot& mTargetSnapshot;
        const Arche::World::WorldReadOnlyView& mReadOnlyWorld;
        const AssetRegistry& mAssetRegistry;
        const std::unordered_map<Arche::EntityID, std::uint32_t>& mSerializedEntityIds;
        const SceneWorldSnapshot::SceneEntitySnapshot& mEntitySnapshot;
    };
}
