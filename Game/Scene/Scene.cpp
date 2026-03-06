#include "Scene.h"

namespace Game {
    Scene::Scene()
        : mName{},
        mWorld{},
        mFrameContext{},
        mAssetRegistry{},
        mSystems{},
        mSystemSceduler{} {
    }

    Scene::~Scene() {
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

    AssetRegistry& Scene::GetAssetRegistry() {
        return mAssetRegistry;
    }

    const AssetRegistry& Scene::GetAssetRegistry() const {
        return mAssetRegistry;
    }

    void Scene::InitializeAssetRegistry(ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator, Core::DX::DescriptorHeap* SrvHeap) {
        mAssetRegistry.Initialize(Device, CopyQueue, Allocator);
        mAssetRegistry.SetSrvHeap(SrvHeap);
        mFrameContext.MaterialGroups = &mAssetRegistry.GetMaterialGroups();
        mFrameContext.RenderData.materials = mAssetRegistry.GetPackedMaterials();
        mFrameContext.RenderData.materialTextureTable = mAssetRegistry.GetMaterialTextureTable();
    }

    void Scene::SetName(const std::string& NewName) {
        mName = NewName;
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
        mAssetRegistry.PrepareRenderTextures(mFrameContext.RenderData);
        mFrameContext.RenderData.materialTextureTable = mAssetRegistry.GetMaterialTextureTable();
    }

    void Scene::ExecutePhase(Phase TargetPhase, float Dt) {
        if (TargetPhase == Phase::PreUpdate) {
            mFrameContext.RenderData.modelContexts.clear();
            mFrameContext.RenderData.drawRecords.clear();
            mFrameContext.RenderData.materials = mAssetRegistry.GetPackedMaterials();
            mFrameContext.RenderData.materialTextureTable = mAssetRegistry.GetMaterialTextureTable();
        }

        const SystemSceduler::PhaseBatchArray* PhaseBatches{ mSystemSceduler.GetPhaseBatches(TargetPhase) };
        if (PhaseBatches == nullptr) {
            return;
        }

        for (const SystemSceduler::SystemBatch& Batch : *PhaseBatches) {
            for (ISystem* System : Batch) {
                System->Execute(mWorld, mFrameContext, Dt);
            }
        }
    }
}
