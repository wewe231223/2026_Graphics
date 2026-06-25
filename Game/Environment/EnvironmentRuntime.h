#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <DirectXCollision.h>
#include <DirectXTK12/SimpleMath.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "Core/Common.h"
#include "Game/Environment/EnvironmentObjectRenderContext.h"
#include "RenderContract/Frame/ShadowMappingParameter.h"
#include "RenderContract/Future/Future.h"

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
        RenderContract::Future mLastGpuDispatchFuture{};
        std::string mConfigPath{};
        std::uint32_t mGpuStatusUavIndex{ 0xffffffffu };
        bool mInitialized{};
        bool mGpuDrivenEnabled{};
        bool mGpuResourcesInitialized{};
    };
}
