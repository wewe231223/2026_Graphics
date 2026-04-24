#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "Arche/World.h"
#include "System.h"
#include "SystemSceduler.h"
#include "SceneWorldSnapshot.h"
#include "Game/Model/AssetRegistry.h"
#include "Script/Core/LuaScriptFramework.h"
#include "PhysicsLib/World/PhysicsWorld.h"

namespace Game {
    struct TerrainActorDescBinding final {
        Arche::EntityID mEntityId{ Arche::NullEntityID };
        PhysicsTerrainActor::ActorDesc mTerrainActorDesc{};
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

        AssetRegistry& GetAssetRegistry();
        const AssetRegistry& GetAssetRegistry() const;
        Script::LuaBehaviorFramework& GetLuaScriptFramework();
        const Script::LuaBehaviorFramework& GetLuaScriptFramework() const;
        PhysicsWorld& GetPhysicsWorld();
        const PhysicsWorld& GetPhysicsWorld() const;

        void InitializeAssetRegistry(ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Core::DX::DescriptorHeap* SrvHeap);
        void InitializePhysicsWorld();
        void RebuildPhysicsActors();
        void UpdatePhysics(float Dt);
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
        void RebuildWorldSnapshot();
        void SpawnModelAtOrigin(const std::string& ModelSelector, const std::string& RootEntityName, std::uint32_t MaterialGroupIndex, bool IsDerivedEntity);

        void RegisterScriptTypes(); 
        void AttachDefaultCameraControlBehavior();
        void UpdateCameraVirtualMouseState();
        void AppendDebugWorldAxes();
    private:
        std::string mName{};
        Arche::World mWorld{};
        PhysicsWorld mPhysicsWorld{};
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

		Script::LuaBehaviorFramework mLuaScriptFramework{};
        std::vector<TerrainActorDescBinding> mTerrainActorDescBindings{};
    };
}
