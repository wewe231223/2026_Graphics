#pragma once
#include <memory>
#include <string>
#include <vector>
#include "Arche/World.h"
#include "System.h"
#include "SystemSceduler.h"
#include "SceneWorldSnapshot.h"
#include "Game/Model/AssetRegistry.h"

namespace Game {
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

        void InitializeAssetRegistry(ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Core::DX::DescriptorHeap* SrvHeap);
        void SetName(const std::string& NewName);
        const std::string& GetName() const;

        void AddSystem(std::unique_ptr<ISystem> NewSystem);
        void BuildSystemExecutionPlan();
        void ExecutePhase(Phase TargetPhase, float Dt);
        void PrepareRender();

        void InitializeWorldSnapshot();
        void UpdateWorldSnapshotIfNeeded();
        const SceneWorldSnapshot& GetWorldSnapshot() const;

    private:
        void RebuildWorldSnapshot();

    private:
        std::string mName{};
        Arche::World mWorld{};
        FrameContext mFrameContext{};
        AssetRegistry mAssetRegistry{};
        std::vector<std::unique_ptr<ISystem>> mSystems{};
        SystemSceduler mSystemSceduler{};

        SceneWorldSnapshot mWorldSnapshot{};
        std::uint64_t mWorldSnapshotVersion{};
        std::uint64_t mHierarchyEntitySelectedSubscriptionId{};
    };
}
