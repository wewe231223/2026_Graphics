#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <DirectXCollision.h>
#include <DirectXTK12/SimpleMath.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "Core/Common.h"
#include "Core/DX/DesciptorHeap.h"
#include "Environment/EnvironmentGpuPlacementData.h"
#include "Environment/EnvironmentObjectRenderContext.h"
#include "RenderContract/Common.h"
#include "RenderContract/Environment/EnvironmentRenderRuntime.h"
#include "RenderContract/Frame/RenderFrameData.h"
#include "RenderContract/Frame/ShadowMappingParameter.h"
#include "RenderContract/Future/Future.h"
#include "Utility/CompileTimeConstants.h"

namespace Arche {
    class World;
}

namespace Game {
    class EnvironmentFoliageRuntime;
    struct FrameContext;

    struct EnvironmentTerrainInput final {
    public:
        ID3D12Resource* mHeightResource{};
        ID3D12Resource* mSplatResource{};
        RenderContract::Future mUploadFuture{};
        std::uint32_t mHeightSrvIndex{ 0xffffffffu };
        std::uint32_t mSplatSrvIndex{ 0xffffffffu };
        std::uint32_t mSplat1SrvIndex{ 0xffffffffu };
        std::uint32_t mWidth{};
        std::uint32_t mHeight{};
        std::uint32_t mSplatWidth{};
        std::uint32_t mSplatHeight{};
        std::uint32_t mSeed{};
        SimpleMath::Vector3 mPosition{ SimpleMath::Vector3::Zero };
        SimpleMath::Vector3 mScale{ SimpleMath::Vector3::One };
        float mCellSizeX{ 1.0f };
        float mCellSizeZ{ 1.0f };
        float mMaxHeight{ 1.0f };
        float mOriginOffsetX{};
        float mOriginOffsetZ{};
    };

    struct EnvironmentPhysicsHandle final {
    public:
        std::uint32_t mIndex{};
        bool mValid{};
    };

    struct EnvironmentPhysicsActorDesc final {
    public:
        SimpleMath::Vector3 mPosition{};
        SimpleMath::Vector3 mExtents{};
        SimpleMath::Vector3 mCenter{};
        float mYawRadians{};
        float mFriction{};
        float mRestitution{};
    };

    class EnvironmentPhysicsAdapter {
    public:
        EnvironmentPhysicsAdapter();
        virtual ~EnvironmentPhysicsAdapter();
        EnvironmentPhysicsAdapter(const EnvironmentPhysicsAdapter& Other) = delete;
        EnvironmentPhysicsAdapter& operator=(const EnvironmentPhysicsAdapter& Other) = delete;
        EnvironmentPhysicsAdapter(EnvironmentPhysicsAdapter&& Other) = delete;
        EnvironmentPhysicsAdapter& operator=(EnvironmentPhysicsAdapter&& Other) = delete;

    public:
        virtual EnvironmentPhysicsHandle CreateOrUpdateStaticActor(std::uint64_t ObjectStableId, const EnvironmentPhysicsActorDesc& Desc) = 0;
        virtual void DeactivateStaticActor(EnvironmentPhysicsHandle Handle) = 0;
    };

    struct EnvironmentRuntimeDesc final {
    public:
        ID3D12Device* mDevice{};
        Interface::IGraphicsAllocator* mAllocator{};
        Interface::IDescriptorHeap* mSrvHeap{};
        Interface::ICopyQueue* mCopyQueue{};
        Interface::IComputeQueue* mComputeQueue{};
        EnvironmentPhysicsAdapter* mPhysicsAdapter{};
        bool mGpuDrivenEnabled{};
    };

    struct EnvironmentFrameInput final {
    public:
        std::uint64_t mFrameIndex{};
        float mDeltaTime{};
        SimpleMath::Vector3 mFocusPosition{ SimpleMath::Vector3::Zero };
        SimpleMath::Matrix mView{ SimpleMath::Matrix::Identity };
        SimpleMath::Matrix mProjection{ SimpleMath::Matrix::Identity };
        SimpleMath::Matrix mViewProjection{ SimpleMath::Matrix::Identity };
        DirectX::BoundingFrustum mCameraFrustum{};
        std::array<DirectX::BoundingFrustum, RenderContract::ShadowCascadeMaxCount> mShadowFrustums{};
        EnvironmentTerrainInput mTerrain{};
    };

    struct EnvironmentGpuDrivenFrameResource final {
    public:
        struct DescriptorCache final {
        public:
            ID3D12Resource* mResource{};
            std::uint32_t mElementCount{};
            std::uint32_t mStride{};
        };

        Microsoft::WRL::ComPtr<ID3D12Resource> mInstanceContextBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mSegmentContextBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mDrawRecordBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mPlacementConfigBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mPlacementRuleBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mPlacementDrawRecordBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mPlacementDrawDispatchRecordBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mPlacementCandidateRecordBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mPlacementCandidateDispatchRecordBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mPlacementSpacingRuleRecordBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mCandidateContextBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mVisibleInstanceIndexBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mIndirectArgumentBuffer{};
        Core::DX::DescriptorHandle mInstanceContextSrvHandle{};
        Core::DX::DescriptorHandle mInstanceContextUavHandle{};
        Core::DX::DescriptorHandle mSegmentContextSrvHandle{};
        Core::DX::DescriptorHandle mDrawRecordSrvHandle{};
        Core::DX::DescriptorHandle mPlacementConfigSrvHandle{};
        Core::DX::DescriptorHandle mPlacementRuleSrvHandle{};
        Core::DX::DescriptorHandle mPlacementDrawRecordSrvHandle{};
        Core::DX::DescriptorHandle mPlacementDrawDispatchRecordSrvHandle{};
        Core::DX::DescriptorHandle mPlacementCandidateRecordSrvHandle{};
        Core::DX::DescriptorHandle mPlacementCandidateDispatchRecordSrvHandle{};
        Core::DX::DescriptorHandle mPlacementSpacingRuleRecordSrvHandle{};
        Core::DX::DescriptorHandle mCandidateContextSrvHandle{};
        Core::DX::DescriptorHandle mCandidateContextUavHandle{};
        Core::DX::DescriptorHandle mVisibleInstanceIndexSrvHandle{};
        Core::DX::DescriptorHandle mVisibleInstanceIndexUavHandle{};
        Core::DX::DescriptorHandle mIndirectArgumentUavHandle{};
        std::uint64_t mInstanceContextBufferCapacityInBytes{};
        std::uint64_t mSegmentContextBufferCapacityInBytes{};
        std::uint64_t mDrawRecordBufferCapacityInBytes{};
        std::uint64_t mPlacementConfigBufferCapacityInBytes{};
        std::uint64_t mPlacementRuleBufferCapacityInBytes{};
        std::uint64_t mPlacementDrawRecordBufferCapacityInBytes{};
        std::uint64_t mPlacementDrawDispatchRecordBufferCapacityInBytes{};
        std::uint64_t mPlacementCandidateRecordBufferCapacityInBytes{};
        std::uint64_t mPlacementCandidateDispatchRecordBufferCapacityInBytes{};
        std::uint64_t mPlacementSpacingRuleRecordBufferCapacityInBytes{};
        std::uint64_t mCandidateContextBufferCapacityInBytes{};
        std::uint64_t mVisibleInstanceIndexBufferCapacityInBytes{};
        std::uint64_t mIndirectArgumentBufferCapacityInBytes{};
        D3D12_RESOURCE_STATES mInstanceContextState{ D3D12_RESOURCE_STATE_COMMON };
        D3D12_RESOURCE_STATES mCandidateContextState{ D3D12_RESOURCE_STATE_COMMON };
        D3D12_RESOURCE_STATES mVisibleInstanceIndexState{ D3D12_RESOURCE_STATE_COMMON };
        D3D12_RESOURCE_STATES mIndirectArgumentState{ D3D12_RESOURCE_STATE_COMMON };
        std::array<std::uint64_t, 10> mUploadedDataHashes{};
        std::array<DescriptorCache, 13> mSrvCaches{};
        std::array<DescriptorCache, 4> mUavCaches{};
    };

    class EnvironmentRuntime final : public RenderContract::IEnvironmentRenderRuntime {
    public:
        EnvironmentRuntime();
        ~EnvironmentRuntime();
        EnvironmentRuntime(const EnvironmentRuntime& Other) = delete;
        EnvironmentRuntime& operator=(const EnvironmentRuntime& Other) = delete;
        EnvironmentRuntime(EnvironmentRuntime&& Other) noexcept;
        EnvironmentRuntime& operator=(EnvironmentRuntime&& Other) noexcept;

    public:
        bool Initialize(const EnvironmentRuntimeDesc& Desc);
        void Reset();
        void SetConfigPath(const std::string& ConfigPath);
        const std::string& GetConfigPath() const;
        void Tick(Arche::World& World, FrameContext& Ctx, float Dt);
        void Tick(const EnvironmentFrameInput& Input, RenderContract::RenderFrameData& RenderData);
        void TickCpu(const EnvironmentFrameInput& Input);
        RenderContract::Future PrepareGpuDrivenFrame(const EnvironmentFrameInput& Input, RenderContract::RenderFrameData& RenderData);
        RenderContract::Future DispatchGpu(const EnvironmentFrameInput& Input);
        void Draw(ID3D12GraphicsCommandList* CommandList);
        void RecordGBuffer(const RenderContract::EnvironmentGBufferRenderCommandContext& Context) override;
        void RecordShadowDepth(const RenderContract::EnvironmentShadowDepthRenderCommandContext& Context) override;
        RenderContract::Future GetEnvironmentGpuFuture() const override;
        EnvironmentObjectRenderContext& GetRenderContext();
        const EnvironmentObjectRenderContext& GetRenderContext() const;
        bool IsInitialized() const;
        bool IsGpuDrivenEnabled() const;

    private:
        bool InitializeGpuResources();
        bool CreateComputeRootSignature();
        bool CreateComputePipelineState();
        bool CreateGpuStatusBuffer();
        bool EnsureDrawIndexedIndirectCommandSignature(ID3D12RootSignature* RootSignature);
        std::vector<D3D12_VERTEX_BUFFER_VIEW> BuildVertexBufferViews(const RenderContract::IPipeline& Pipeline, const RenderContract::IModelNode& Mesh) const;
        const std::vector<D3D12_VERTEX_BUFFER_VIEW>& ResolveVertexBufferViews(const RenderContract::IPipeline& Pipeline, const RenderContract::IModelNode& Mesh);
        void RecordGBufferDirect(const RenderContract::EnvironmentGBufferRenderCommandContext& Context);
        void RecordGBufferIndirect(const RenderContract::EnvironmentGBufferRenderCommandContext& Context);
        void RecordShadowDepthIndirect(const RenderContract::EnvironmentShadowDepthRenderCommandContext& Context);
        bool EnsureGpuDrivenDescriptorHandles(EnvironmentGpuDrivenFrameResource& FrameResource);
        bool EnsureGpuDrivenBuffer(Microsoft::WRL::ComPtr<ID3D12Resource>& Buffer, std::uint64_t& CapacityInBytes, std::uint64_t RequiredSizeInBytes, D3D12_RESOURCE_FLAGS ResourceFlags, D3D12_RESOURCE_STATES InitialState, const wchar_t* ResourceName);
        bool EnsureGpuDrivenFrameResources(EnvironmentGpuDrivenFrameResource& FrameResource, std::uint32_t InstanceContextCount, std::uint32_t SegmentContextCount, std::uint32_t DrawRecordCount, std::uint32_t PlacementConfigCount, std::uint32_t PlacementRuleCount, std::uint32_t PlacementDrawRecordCount, std::uint32_t PlacementDrawDispatchRecordCount, std::uint32_t PlacementCandidateRecordCount, std::uint32_t PlacementCandidateDispatchRecordCount, std::uint32_t PlacementSpacingRuleRecordCount, std::uint32_t CandidateContextCount, std::uint32_t VisibleInstanceIndexCount);
        void BuildGpuDrivenFrameData(const EnvironmentFrameInput& Input, RenderContract::RenderFrameData& RenderData, std::uint32_t& OutVisibleInstanceIndexCount);
        RenderContract::Future UploadGpuDrivenFrameData(EnvironmentGpuDrivenFrameResource& FrameResource);
        RenderContract::Future DispatchGpuDrivenFrame(EnvironmentGpuDrivenFrameResource& FrameResource, const EnvironmentFrameInput& Input, const RenderContract::Future& CopyFuture, std::uint32_t DrawRecordCount, std::uint32_t VisibleInstanceIndexCapacity, std::uint32_t CandidateRecordCount, std::uint32_t CandidateDispatchRecordCount, std::uint32_t DrawDispatchRecordCount, std::uint32_t SpacingRuleRecordCount);
        void FillGpuDrivenFramePayload(EnvironmentGpuDrivenFrameResource& FrameResource, RenderContract::RenderFrameData& RenderData, const RenderContract::Future& GpuDispatchFuture);
        void UpdateGpuDrivenShaderResourceViews(EnvironmentGpuDrivenFrameResource& FrameResource, std::uint32_t InstanceContextCount, std::uint32_t SegmentContextCount, std::uint32_t DrawRecordCount, std::uint32_t PlacementConfigCount, std::uint32_t PlacementRuleCount, std::uint32_t PlacementDrawRecordCount, std::uint32_t PlacementDrawDispatchRecordCount, std::uint32_t PlacementCandidateRecordCount, std::uint32_t PlacementCandidateDispatchRecordCount, std::uint32_t PlacementSpacingRuleRecordCount, std::uint32_t CandidateContextCount, std::uint32_t VisibleInstanceIndexCount);
        void UpdateGpuDrivenUnorderedAccessViews(EnvironmentGpuDrivenFrameResource& FrameResource, std::uint32_t InstanceContextCount, std::uint32_t CandidateContextCount, std::uint32_t VisibleInstanceIndexCount, std::uint32_t IndirectArgumentCount);
        void ResetGpuResources();

    private:
        ID3D12Device* mDevice{};
        Interface::IGraphicsAllocator* mAllocator{};
        Interface::IDescriptorHeap* mSrvHeap{};
        Interface::ICopyQueue* mCopyQueue{};
        Interface::IComputeQueue* mComputeQueue{};
        EnvironmentPhysicsAdapter* mPhysicsAdapter{};
        EnvironmentObjectRenderContext mRenderContext{};
        Microsoft::WRL::ComPtr<ID3D12RootSignature> mComputeRootSignature{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> mIndirectCommandInitializePipelineState{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> mCandidateGeneratePipelineState{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> mCandidateClassifyPipelineState{};
        Microsoft::WRL::ComPtr<ID3D12CommandSignature> mDrawIndexedIndirectCommandSignature{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mGpuStatusBuffer{};
        Core::DX::DescriptorHandle mGpuStatusUavHandle{};
        RenderContract::Future mLastGpuDispatchFuture{};
        std::string mConfigPath{};
        std::unique_ptr<EnvironmentFoliageRuntime> mFoliageRuntime{};
        std::vector<RenderContract::EnvironmentInstanceContext> mGpuInstanceContexts{};
        std::vector<RenderContract::EnvironmentSegmentContext> mGpuSegmentContexts{};
        std::vector<RenderContract::EnvironmentDrawRecordGpu> mGpuDrawRecords{};
        std::vector<D3D12_DRAW_INDEXED_ARGUMENTS> mGpuIndirectArguments{};
        EnvironmentGpuPlacementFrameData mGpuPlacementFrameData{};
        std::map<std::pair<const RenderContract::IPipeline*, const RenderContract::IModelNode*>, std::vector<D3D12_VERTEX_BUFFER_VIEW>> mVertexBufferViewCache{};
        std::array<EnvironmentGpuDrivenFrameResource, Constants::FrameCount<std::size_t>> mGpuDrivenFrameResources{};
        std::uint32_t mGpuInstanceContextCount{};
        std::uint32_t mGpuStatusUavIndex{ 0xffffffffu };
        bool mInitialized{};
        bool mGpuDrivenEnabled{};
        bool mGpuResourcesInitialized{};
    };
}
