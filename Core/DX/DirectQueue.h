#pragma once
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include "Core/Common.h"
#include "Core/DX/DesciptorHeap.h"
#include "Core/DX/DrawCallDispatcher.h"
#include "Core/DX/DrawCallResourceManager.h"
#include "Core/DX/MaterialResourceManager.h"
#include "Core/DX/FrameSync.h"
#include "Core/DX/Texture.h"
#include "Core/Config.h"
#include "Utility/DirectXInclude.h"
#include "Utility/CompileTimeConstants.h"
#include "Utility/FixedArray.h"
#include "RenderContract/Frame/RenderFrameData.h"

#include "Widget/WidgetCore.h"

namespace Core {
	namespace DX {
		class GraphicsAllocator;

		struct PostProcessRootConstants final {
			std::uint32_t mSourceSrvIndex{};
			std::uint32_t mDestinationUavIndex{};
			std::uint32_t mTargetWidth{};
			std::uint32_t mTargetHeight{};
			std::uint32_t mParameter0{};
			std::uint32_t mParameter1{};
			std::uint32_t mParameter2{};
			std::uint32_t mParameter3{};
			std::uint32_t mParameter4{};
			std::uint32_t mParameter5{};
			std::uint32_t mParameter6{};
			std::uint32_t mParameter7{};
		};

		struct PostProcessJob final {
			std::string mPipelineName{};
			TexPtr mSourceTarget{};
			TexPtr mDestinationTarget{};
			PostProcessRootConstants mRootConstants{};
			std::uint32_t mThreadGroupSizeX{ 8u };
			std::uint32_t mThreadGroupSizeY{ 8u };
			std::uint32_t mThreadGroupSizeZ{ 1u };
		};

		class DirectQueue : public RenderContract::IFutureSyncObject {
		public:
			DirectQueue(HWND hWnd);
			~DirectQueue();

			DirectQueue(const DirectQueue& Other) = delete;
			DirectQueue& operator=(const DirectQueue& Other) = delete;

			DirectQueue(DirectQueue&& Other) = delete;
			DirectQueue& operator=(DirectQueue&& Other) = delete;

		public:
			ID3D12Device* GetDevice() const;

			IDXGIAdapter1* GetPrimaryAdapter() const;

			ID3D12CommandQueue* GetCommandQueue() const;

			std::uint32_t GetCurrentFrameIndex() const;

			DescriptorHeap* GetSrvHeap();

			void QueueWaitFuture(const RenderContract::Future& Future) const;
			bool IsFutureComplete(std::uint64_t DirectTicket) const override;
			void WaitFuture(std::uint64_t DirectTicket) const override;
			void QueueWaitFuture(ID3D12CommandQueue* WaitingQueue, std::uint64_t DirectTicket) const override;
			RenderContract::Future SignalFuture();
			void SetComputeQueue(Interface::IComputeQueue* ComputeQueue);
			void SetUploadInfrastructure(GraphicsAllocator* GraphicsAllocator, Interface::ICopyQueue* CopyQueue);
			void PreRender(RenderContract::RenderFrameData& Data, float Dt);
			void Render(RenderContract::RenderFrameData& Data, Widget::WidgetCore* WidgetCore);

		private:
			void InitBasements();
			void InitWorkers();
			void InitCommandList();
			void InitGpuTimestampQuery();
			void InitTargetResources();
			void ResolveGpuFrameTime(std::uint32_t FrameIndex);
			void BeginGpuFrameTimestampQuery(std::uint32_t FrameIndex);
			void EndGpuFrameTimestampQuery(std::uint32_t FrameIndex);
			void InitGBufferResources();
			void EnsureShadowMapResources(const RenderContract::ShadowMappingParameter& ShadowMappingParameter);
			PostProcessJob BuildToneMappingPostProcessJob(const TexPtr& SourceTarget, const TexPtr& DestinationTarget, bool IsPostProcessEnabled);
			void PreparePostProcessJobResources(ID3D12GraphicsCommandList* CommandList, const PostProcessJob& Job);
			RenderContract::Future EnqueuePostProcessJob(const RenderContract::Future& WaitFuture, const PostProcessJob& Job);
			Game::Base::Pipeline* ResolvePostProcessPipeline(const std::string& PipelineName);
			void BeginPostProcessFinalPass(std::uint32_t CurrentIndex, const std::array<ID3D12DescriptorHeap*, 1>& DescriptorHeaps, const RenderContract::Future& PostProcessFuture);
			void CopyPostProcessToBackBuffer(const TexPtr& PostProcessTarget, const TexPtr& RenderTarget);
			void DrawFinalOverlays(RenderContract::RenderFrameData& Data, Widget::WidgetCore* WidgetCore, DrawCallResourceManager& DrawCallResources, D3D12_CPU_DESCRIPTOR_HANDLE Dsv, std::uint32_t CurrentIndex, std::uint32_t ShadowCascadeCount, bool IsPerformanceEnabled);
			void FinishPresentTarget(const TexPtr& RenderTarget);
			void ExecutePostProcessFinalPass();

			ComPtr<IDXGIAdapter1> GetBestAdapter();

			bool CheckShaderModelSupport(D3D_SHADER_MODEL);

			void DrainDebugMessages();

		private:
			static constexpr std::uint32_t GBufferTargetCount{ 4 };
			static constexpr std::uint32_t GBufferAlbedoIndex{ 0 };
			static constexpr std::uint32_t GBufferNormalIndex{ 1 };
			static constexpr std::uint32_t GBufferWorldPositionIndex{ 2 };
			static constexpr std::uint32_t GBufferMotionVectorIndex{ 3 };
			static constexpr std::uint32_t GpuTimestampCountPerFrame{ 2 };

			HWND mHwnd{ nullptr };
			ComPtr<IDXGIFactory6> mFactory{ nullptr };

		#if defined(DEBUG) || defined(_DEBUG)
			ComPtr<ID3D12Debug6> mDebugController{ nullptr };
			ComPtr<IDXGIDebug1> mDebugDXGI{ nullptr };
			ComPtr<IDXGIInfoQueue> mDxgiInfoQueue{ nullptr };
			ComPtr<ID3D12InfoQueue> mD3D12InfoQueue{ nullptr };
		#endif
			ComPtr<ID3D12Device> mDevice{ nullptr };
			ComPtr<IDXGIAdapter1> mPrimaryAdapter{ nullptr };
			ComPtr<ID3D12CommandQueue> mDirectCommandQueue{ nullptr };
			ComPtr<ID3D12Fence> mDirectFence{ nullptr };
			HANDLE mDirectFenceEvent{ nullptr };
			mutable std::mutex mDirectFenceMutex{};
			std::atomic<std::uint64_t> mDirectFenceValueCounter{};

			ComPtr<IDXGISwapChain1> mSwapChain{ nullptr };

			ComPtr<ID3D12GraphicsCommandList> mCommandList{ nullptr };
			ComPtr<ID3D12GraphicsCommandList9> mCommandList9{ nullptr };
			std::array<ComPtr<ID3D12CommandAllocator>, Constants::FrameCount<size_t>> mMainCommandAllocators{};
			ComPtr<ID3D12GraphicsCommandList> mEnvironmentCommandList{ nullptr };
			ComPtr<ID3D12GraphicsCommandList9> mEnvironmentCommandList9{ nullptr };
			std::array<ComPtr<ID3D12CommandAllocator>, Constants::FrameCount<size_t>> mEnvironmentCommandAllocators{};
			ComPtr<ID3D12GraphicsCommandList> mPostProcessCommandList{ nullptr };
			std::array<ComPtr<ID3D12CommandAllocator>, Constants::FrameCount<size_t>> mPostProcessCommandAllocators{};
			ComPtr<ID3D12QueryHeap> mGpuTimestampQueryHeap{ nullptr };
			ComPtr<ID3D12Resource> mGpuTimestampReadbackBuffer{ nullptr };
			std::array<std::uint64_t, Constants::FrameCount<size_t>> mGpuTimestampFrameIdentifiers{};
			std::array<bool, Constants::FrameCount<size_t>> mHasGpuTimestampFrame{};
			std::uint64_t mGpuTimestampFrequency{};

			DescriptorHeap mRTVHeap{};
			std::array<TexPtr, Constants::FrameCount<size_t>> mRenderTargets{};
			std::array<TexPtr, Constants::FrameCount<size_t>> mLightingTargets{};
			std::array<TexPtr, Constants::FrameCount<size_t>> mPostProcessTargets{};
			std::array<TexPtr, GBufferTargetCount> mGBufferTargets{};
			uint32_t mRTVIndex{};

			DescriptorHeap mDSVHeap{};
			TexPtr mDepthStencilBuffer{};
			DescriptorHeap mShadowDSVHeap{};
			std::array<TexPtr, RenderContract::ShadowCascadeMaxCount> mShadowDepthMaps{};
			uint32_t mShadowCascadeCount{};
			std::array<uint32_t, RenderContract::ShadowCascadeMaxCount> mShadowMapSizes{};
			DescriptorHandle mShadowMapBaseSrvHandle{};
			std::array<DescriptorHandle, RenderContract::ShadowCascadeMaxCount> mShadowMapSrvHandles{};
			DescriptorHeap mSrvHeap{};
			std::array<DrawCallResourceManager, Constants::FrameCount<size_t>> mDrawCallResourceManagers{};
			MaterialResourceManager mMaterialResourceManager{};
			DrawCallDispatcher mDrawCallDispatcher{};
			std::unordered_map<std::string, Game::Base::Pipeline> mPostProcessPipelines{};


			FrameSync mFrameSync{};
			GraphicsAllocator* mGraphicsAllocator{ nullptr };
			Interface::ICopyQueue* mCopyQueue{ nullptr };
			Interface::IComputeQueue* mComputeQueue{ nullptr };
			bool mIsDynamicDepthBiasSupported{};


			D3D12_VIEWPORT mViewport{ 0, 0, Config::Query()->Get<float>("Window_Width"), Config::Query()->Get<float>("Window_Height"), 0.f, 1.f };
			D3D12_RECT mScissorRect{ 0, 0, Config::Query()->Get<LONG>("Window_Width"), Config::Query()->Get<LONG>("Window_Height") };
			std::array<D3D12_VIEWPORT, RenderContract::ShadowCascadeMaxCount> mShadowViewports{};
			std::array<D3D12_RECT, RenderContract::ShadowCascadeMaxCount> mShadowScissorRects{};
		};

	}
}
