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
#include "Game/Base/RenderFrameData.h"

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

		class DirectQueue : public Interface::IFutureSyncObject {
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

			DescriptorHeap* GetSrvHeap();

			void QueueWaitFuture(const Interface::Future& Future) const;
			bool IsFutureComplete(std::uint64_t DirectTicket) const override;
			void WaitFuture(std::uint64_t DirectTicket) const override;
			void QueueWaitFuture(ID3D12CommandQueue* WaitingQueue, std::uint64_t DirectTicket) const override;
			Interface::Future SignalFuture();
			void SetComputeQueue(Interface::IComputeQueue* ComputeQueue);
			void SetUploadInfrastructure(GraphicsAllocator* GraphicsAllocator, Interface::ICopyQueue* CopyQueue);
			void PreRender(Game::RFD::RenderFrameData& Data, float Dt);
			void Render(Game::RFD::RenderFrameData& Data, Widget::WidgetCore* WidgetCore);

		private:
			void InitBasements();
			void InitWorkers();
			void InitCommandList();
			void InitTargetResources();
			void InitGBufferResources();
			void EnsureShadowMapResources(const Game::RFD::ShadowMappingParameter& ShadowMappingParameter);
			PostProcessJob BuildToneMappingPostProcessJob(const TexPtr& SourceTarget, const TexPtr& DestinationTarget, bool IsPostProcessEnabled);
			void PreparePostProcessJobResources(ID3D12GraphicsCommandList* CommandList, const PostProcessJob& Job);
			Interface::Future EnqueuePostProcessJob(const Interface::Future& WaitFuture, const PostProcessJob& Job);
			Game::Base::Pipeline* ResolvePostProcessPipeline(const std::string& PipelineName);
			void BeginPostProcessFinalPass(std::uint32_t CurrentIndex, const std::array<ID3D12DescriptorHeap*, 1>& DescriptorHeaps, const Interface::Future& PostProcessFuture);
			void CopyPostProcessToBackBuffer(const TexPtr& PostProcessTarget, const TexPtr& RenderTarget);
			void DrawFinalOverlays(Game::RFD::RenderFrameData& Data, Widget::WidgetCore* WidgetCore, DrawCallResourceManager& DrawCallResources, D3D12_CPU_DESCRIPTOR_HANDLE Dsv, std::uint32_t CurrentIndex, std::uint32_t ShadowCascadeCount);
			void FinishPresentTarget(const TexPtr& RenderTarget, DrawCallResourceManager& DrawCallResources, std::uint32_t CurrentIndex);
			void ExecutePostProcessFinalPass();

			ComPtr<IDXGIAdapter1> GetBestAdapter();

			bool CheckShaderModelSupport(D3D_SHADER_MODEL);

			void DrainDebugMessages();

		private:
			static constexpr std::uint32_t GBufferTargetCount{ 3 };
			static constexpr std::uint32_t GBufferAlbedoIndex{ 0 };
			static constexpr std::uint32_t GBufferNormalIndex{ 1 };
			static constexpr std::uint32_t GBufferWorldPositionIndex{ 2 };

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
			std::array<ComPtr<ID3D12CommandAllocator>, Constants::FrameCount<size_t>> mMainCommandAllocators{};
			ComPtr<ID3D12GraphicsCommandList> mPostProcessCommandList{ nullptr };
			std::array<ComPtr<ID3D12CommandAllocator>, Constants::FrameCount<size_t>> mPostProcessCommandAllocators{};

			DescriptorHeap mRTVHeap{};
			std::array<TexPtr, Constants::FrameCount<size_t>> mRenderTargets{};
			std::array<TexPtr, Constants::FrameCount<size_t>> mLightingTargets{};
			std::array<TexPtr, Constants::FrameCount<size_t>> mPostProcessTargets{};
			std::array<TexPtr, GBufferTargetCount> mGBufferTargets{};
			uint32_t mRTVIndex{};

			DescriptorHeap mDSVHeap{};
			TexPtr mDepthStencilBuffer{};
			DescriptorHeap mShadowDSVHeap{};
			std::array<TexPtr, Game::RFD::ShadowCascadeMaxCount> mShadowDepthMaps{};
			uint32_t mShadowCascadeCount{};
			std::array<uint32_t, Game::RFD::ShadowCascadeMaxCount> mShadowMapSizes{};
			DescriptorHandle mShadowMapBaseSrvHandle{};
			std::array<DescriptorHandle, Game::RFD::ShadowCascadeMaxCount> mShadowMapSrvHandles{};
			DescriptorHeap mSrvHeap{};
			std::array<DrawCallResourceManager, Constants::FrameCount<size_t>> mDrawCallResourceManagers{};
			MaterialResourceManager mMaterialResourceManager{};
			DrawCallDispatcher mDrawCallDispatcher{};
			std::unordered_map<std::string, Game::Base::Pipeline> mPostProcessPipelines{};


			FrameSync mFrameSync{};
			GraphicsAllocator* mGraphicsAllocator{ nullptr };
			Interface::ICopyQueue* mCopyQueue{ nullptr };
			Interface::IComputeQueue* mComputeQueue{ nullptr };


			D3D12_VIEWPORT mViewport{ 0, 0, Config::Query()->Get<float>("Window_Width"), Config::Query()->Get<float>("Window_Height"), 0.f, 1.f };
			D3D12_RECT mScissorRect{ 0, 0, Config::Query()->Get<LONG>("Window_Width"), Config::Query()->Get<LONG>("Window_Height") };
			std::array<D3D12_VIEWPORT, Game::RFD::ShadowCascadeMaxCount> mShadowViewports{};
			std::array<D3D12_RECT, Game::RFD::ShadowCascadeMaxCount> mShadowScissorRects{};
		};

	}
}
