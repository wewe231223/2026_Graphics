#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <DirectXCollision.h>
#include <DirectXTK12/SimpleMath.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "Core/Common.h"
#include "Core/DX/DesciptorHeap.h"
#include "Game/Environment/EnvironmentObjectRenderContext.h"
#include "RenderContract/Frame/RenderFrameData.h"
#include "RenderContract/Frame/ShadowMappingParameter.h"
#include "RenderContract/Future/Future.h"
#include "Utility/CompileTimeConstants.h"

namespace Game {
    struct EnvironmentTerrainInput final {
    public:
        ID3D12Resource* mHeightResource{};
        ID3D12Resource* mSplatResource{};
        std::uint32_t mHeightSrvIndex{ 0xffffffffu };
        std::uint32_t mSplatSrvIndex{ 0xffffffffu };
        std::uint32_t mWidth{};
        std::uint32_t mHeight{};
        float mCellSizeX{ 1.0f };
        float mCellSizeZ{ 1.0f };
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
        Microsoft::WRL::ComPtr<ID3D12Resource> mInstanceContextBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mSegmentContextBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mDrawRecordBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mVisibleInstanceIndexBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mIndirectArgumentBuffer{};
        Core::DX::DescriptorHandle mInstanceContextSrvHandle{};
        Core::DX::DescriptorHandle mSegmentContextSrvHandle{};
        Core::DX::DescriptorHandle mDrawRecordSrvHandle{};
        Core::DX::DescriptorHandle mVisibleInstanceIndexSrvHandle{};
        Core::DX::DescriptorHandle mVisibleInstanceIndexUavHandle{};
        Core::DX::DescriptorHandle mIndirectArgumentUavHandle{};
        std::uint64_t mInstanceContextBufferCapacityInBytes{};
        std::uint64_t mSegmentContextBufferCapacityInBytes{};
        std::uint64_t mDrawRecordBufferCapacityInBytes{};
        std::uint64_t mVisibleInstanceIndexBufferCapacityInBytes{};
        std::uint64_t mIndirectArgumentBufferCapacityInBytes{};
        D3D12_RESOURCE_STATES mVisibleInstanceIndexState{ D3D12_RESOURCE_STATE_COMMON };
        D3D12_RESOURCE_STATES mIndirectArgumentState{ D3D12_RESOURCE_STATE_COMMON };
    };

    class EnvironmentRuntime final {
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
        void TickCpu(const EnvironmentFrameInput& Input);
        RenderContract::Future PrepareGpuDrivenFrame(const EnvironmentFrameInput& Input, RenderContract::RenderFrameData& RenderData);
        RenderContract::Future DispatchGpu(const EnvironmentFrameInput& Input);
        void Draw(ID3D12GraphicsCommandList* CommandList);
        EnvironmentObjectRenderContext& GetRenderContext();
        const EnvironmentObjectRenderContext& GetRenderContext() const;
        bool IsInitialized() const;
        bool IsGpuDrivenEnabled() const;

    private:
        bool InitializeGpuResources();
        bool CreateComputeRootSignature();
        bool CreateComputePipelineState();
        bool CreateGpuStatusBuffer();
        bool EnsureGpuDrivenDescriptorHandles(EnvironmentGpuDrivenFrameResource& FrameResource);
        bool EnsureGpuDrivenBuffer(Microsoft::WRL::ComPtr<ID3D12Resource>& Buffer, std::uint64_t& CapacityInBytes, std::uint64_t RequiredSizeInBytes, D3D12_RESOURCE_FLAGS ResourceFlags, D3D12_RESOURCE_STATES InitialState, const wchar_t* ResourceName);
        bool EnsureGpuDrivenFrameResources(EnvironmentGpuDrivenFrameResource& FrameResource, std::uint32_t InstanceContextCount, std::uint32_t SegmentContextCount, std::uint32_t DrawRecordCount, std::uint32_t VisibleInstanceIndexCount);
        void BuildGpuDrivenFrameData(RenderContract::RenderFrameData& RenderData, std::uint32_t& OutVisibleInstanceIndexCount);
        RenderContract::Future UploadGpuDrivenFrameData(EnvironmentGpuDrivenFrameResource& FrameResource);
        RenderContract::Future DispatchGpuDrivenFrame(EnvironmentGpuDrivenFrameResource& FrameResource, const EnvironmentFrameInput& Input, const RenderContract::Future& CopyFuture, std::uint32_t DrawRecordCount, std::uint32_t VisibleInstanceIndexCapacity);
        void FillGpuDrivenFramePayload(EnvironmentGpuDrivenFrameResource& FrameResource, RenderContract::RenderFrameData& RenderData, const RenderContract::Future& GpuDispatchFuture);
        void UpdateGpuDrivenShaderResourceViews(EnvironmentGpuDrivenFrameResource& FrameResource, std::uint32_t InstanceContextCount, std::uint32_t SegmentContextCount, std::uint32_t DrawRecordCount, std::uint32_t VisibleInstanceIndexCount);
        void UpdateGpuDrivenUnorderedAccessViews(EnvironmentGpuDrivenFrameResource& FrameResource, std::uint32_t VisibleInstanceIndexCount, std::uint32_t IndirectArgumentCount);
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
        Microsoft::WRL::ComPtr<ID3D12PipelineState> mPreparePipelineState{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mGpuStatusBuffer{};
        Core::DX::DescriptorHandle mGpuStatusUavHandle{};
        RenderContract::Future mLastGpuDispatchFuture{};
        std::string mConfigPath{};
        std::vector<RenderContract::EnvironmentInstanceContext> mGpuInstanceContexts{};
        std::vector<RenderContract::EnvironmentSegmentContext> mGpuSegmentContexts{};
        std::vector<RenderContract::EnvironmentDrawRecordGpu> mGpuDrawRecords{};
        std::vector<D3D12_DRAW_INDEXED_ARGUMENTS> mGpuIndirectArguments{};
        std::array<EnvironmentGpuDrivenFrameResource, Constants::FrameCount<std::size_t>> mGpuDrivenFrameResources{};
        std::uint32_t mGpuStatusUavIndex{ 0xffffffffu };
        bool mInitialized{};
        bool mGpuDrivenEnabled{};
        bool mGpuResourcesInitialized{};
    };
}
