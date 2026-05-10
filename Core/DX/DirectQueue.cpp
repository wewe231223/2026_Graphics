#include "DirectQueue.h"
#include "Utility/ErrorHandler.h"
#include "Utility/Views.h"
#include "Utility/StringUtils.h"
#include "Utility/StdOutput.h"
#include "Core/Config.h"
#include <algorithm>
#include <bit>
#include <fstream>
#include <utility>
#include "Widget/PerformanceProvider.h"

namespace {
	static_assert(sizeof(Core::DX::PostProcessRootConstants) == (sizeof(std::uint32_t) * 12ULL));

	std::uint32_t DivideRoundUp(std::uint32_t Value, std::uint32_t Divisor) {
		return (Value + Divisor - 1u) / Divisor;
	}

	struct ShadowDepthBiasParameter final {
		float DepthBias{};
		float DepthBiasClamp{};
		float SlopeScaledDepthBias{};
	};

	ShadowDepthBiasParameter BuildShadowDepthBiasParameter(const Game::RFD::ShadowMappingParameter& ShadowMappingParameter, std::uint32_t ShadowCascadeIndex) {
		const std::uint32_t ClampedShadowCascadeIndex{ std::min<std::uint32_t>(ShadowCascadeIndex, Game::RFD::ShadowCascadeMaxCount - 1u) };
		const float EffectiveShadowMapSize{ std::max(ShadowMappingParameter.shadowMapSizes[ClampedShadowCascadeIndex], ShadowMappingParameter.minimumShadowMapSize) };
		ShadowDepthBiasParameter Parameter{};
		Parameter.DepthBias = std::max(ShadowMappingParameter.rasterDepthBiases[ClampedShadowCascadeIndex], 0.0f);
		Parameter.SlopeScaledDepthBias = std::max(ShadowMappingParameter.rasterSlopeScaledDepthBiases[ClampedShadowCascadeIndex], 0.0f);
		if (EffectiveShadowMapSize > 0.0f) {
			const float MinimumDepthBiasClamp{ 1.0f / EffectiveShadowMapSize };
			const float MaximumDepthBiasClamp{ 2.0f / EffectiveShadowMapSize };
			const float RequestedDepthBiasClamp{ std::max(ShadowMappingParameter.shadowBiases[ClampedShadowCascadeIndex], 0.0f) };
			Parameter.DepthBiasClamp = std::clamp(RequestedDepthBiasClamp, MinimumDepthBiasClamp, MaximumDepthBiasClamp);
		}

		return Parameter;
	}
}


namespace Core {
    namespace DX {
		DirectQueue::DirectQueue(HWND hWnd) {
			mHwnd = hWnd;
			DirectQueue::InitBasements();
			DirectQueue::InitWorkers();
			DirectQueue::InitCommandList();
			DirectQueue::InitTargetResources();

        }

        DirectQueue::~DirectQueue() {
			if (mDirectFenceEvent != nullptr) {
				CloseHandle(mDirectFenceEvent);
				mDirectFenceEvent = nullptr;
			}
        }

		ID3D12Device* DirectQueue::GetDevice() const {
			return mDevice.Get();
		}

		IDXGIAdapter1* DirectQueue::GetPrimaryAdapter() const {
			return mPrimaryAdapter.Get();
		}

		ID3D12CommandQueue* DirectQueue::GetCommandQueue() const {
			return mDirectCommandQueue.Get();
		}

		std::uint32_t DirectQueue::GetCurrentFrameIndex() const {
			return mFrameSync.GetCurrentIndex();
		}

		DescriptorHeap* DirectQueue::GetSrvHeap() {
			return &mSrvHeap;
		}

		void DirectQueue::QueueWaitFuture(const Interface::Future& Future) const {
			Future.QueueWait(mDirectCommandQueue.Get());
		}

		bool DirectQueue::IsFutureComplete(std::uint64_t DirectTicket) const {
			if (DirectTicket == 0 || mDirectFence == nullptr) {
				return true;
			}

			return mDirectFence->GetCompletedValue() >= DirectTicket;
		}

		void DirectQueue::WaitFuture(std::uint64_t DirectTicket) const {
			if (IsFutureComplete(DirectTicket) == true || mDirectFence == nullptr || mDirectFenceEvent == nullptr) {
				return;
			}

			std::lock_guard<std::mutex> FenceGuard{ mDirectFenceMutex };
			ErrorHandler::report(mDirectFence->SetEventOnCompletion(DirectTicket, mDirectFenceEvent), "DirectQueue", "Failed to set direct queue fence completion event.", ErrorHandler::Level::Critical);
			WaitForSingleObjectEx(mDirectFenceEvent, INFINITE, FALSE);
		}

		void DirectQueue::QueueWaitFuture(ID3D12CommandQueue* WaitingQueue, std::uint64_t DirectTicket) const {
			if (WaitingQueue == nullptr || DirectTicket == 0 || mDirectFence == nullptr) {
				return;
			}

			ErrorHandler::report(WaitingQueue->Wait(mDirectFence.Get(), DirectTicket), "DirectQueue", "Failed to queue wait for direct queue fence.", ErrorHandler::Level::Critical);
		}

		Interface::Future DirectQueue::SignalFuture() {
			if (mDirectCommandQueue == nullptr || mDirectFence == nullptr) {
				return Interface::Future{};
			}

			std::uint64_t DirectTicket{ mDirectFenceValueCounter.fetch_add(1) + 1 };
			ErrorHandler::report(mDirectCommandQueue->Signal(mDirectFence.Get(), DirectTicket), "DirectQueue", "Failed to signal direct queue fence.", ErrorHandler::Level::Critical);
			return Interface::Future{ this, DirectTicket };
		}

		void DirectQueue::SetComputeQueue(Interface::IComputeQueue* ComputeQueue) {
			mComputeQueue = ComputeQueue;
		}

		void DirectQueue::SetUploadInfrastructure(GraphicsAllocator* GraphicsAllocator, Interface::ICopyQueue* CopyQueue) {
			mGraphicsAllocator = GraphicsAllocator;
			mCopyQueue = CopyQueue;
		}

		void DirectQueue::PreRender(Game::RFD::RenderFrameData& Data, float Dt) {
			Data.globals.dt = Dt;
			mRTVIndex = static_cast<uint32_t>(mFrameSync.GetCurrentIndex());

			ErrorHandler::report(mGraphicsAllocator == nullptr, "DirectQueue", "GraphicsAllocator is not set.", ErrorHandler::Level::Critical);
			ErrorHandler::report(mCopyQueue == nullptr, "DirectQueue", "CopyQueue is not set.", ErrorHandler::Level::Critical);

			DrawCallResourceManager& DrawCallResources{ mDrawCallResourceManagers[mRTVIndex] };
			mMaterialResourceManager.PrepareFrameResources(mRTVIndex, Data, *mGraphicsAllocator, mCopyQueue);
			DrawCallResources.PrepareFrameResources(Data, *mGraphicsAllocator, mCopyQueue);

			mCopyQueue->DispatchCopies();
		}


		void DirectQueue::Render(Game::RFD::RenderFrameData& Data, Widget::WidgetCore* WidgetCore) {
			ErrorHandler::report(mCopyQueue == nullptr, "DirectQueue", "CopyQueue is not set.", ErrorHandler::Level::Critical);
			ErrorHandler::report(mComputeQueue == nullptr, "DirectQueue", "ComputeQueue is not set.", ErrorHandler::Level::Critical);

			std::uint32_t CurrentIndex{ mFrameSync.GetCurrentIndex() };

			ComPtr<ID3D12CommandAllocator>& MainCommandAllocator{ mMainCommandAllocators[CurrentIndex] };
			MainCommandAllocator->Reset();
			mCommandList->Reset(MainCommandAllocator.Get(), nullptr);

			std::array<ID3D12DescriptorHeap*, 1> DescriptorHeaps{ mSrvHeap.GetHeap() };
			mCommandList->SetDescriptorHeaps(static_cast<UINT>(DescriptorHeaps.size()), DescriptorHeaps.data());

			DrawCallResourceManager& DrawCallResources{ mDrawCallResourceManagers[CurrentIndex] };
			mMaterialResourceManager.TransitionToShaderResource(mCommandList.Get(), static_cast<std::uint32_t>(CurrentIndex));
			DrawCallResources.TransitionToShaderResource(mCommandList.Get());

			EnsureShadowMapResources(Data.shadowMapping);
			const std::uint32_t ShadowCascadeCount{ std::max<std::uint32_t>(1u, std::min<std::uint32_t>(Data.shadowMapping.cascadeCount, mShadowCascadeCount)) };
			for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1) {
				TexPtr& ShadowDepthMap{ mShadowDepthMaps[CascadeIndex] };
				if (ShadowDepthMap == nullptr) {
					continue;
				}

				ShadowDepthMap->Transition(mCommandList.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

				D3D12_CPU_DESCRIPTOR_HANDLE ShadowDsv{ ShadowDepthMap->GetDSV() };
				mCommandList->ClearDepthStencilView(ShadowDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
				mCommandList->OMSetRenderTargets(0, nullptr, FALSE, &ShadowDsv);
				mCommandList->RSSetViewports(1, &mShadowViewports[CascadeIndex]);
				mCommandList->RSSetScissorRects(1, &mShadowScissorRects[CascadeIndex]);
				const ShadowDepthBiasParameter ShadowDepthBias{ BuildShadowDepthBiasParameter(Data.shadowMapping, CascadeIndex) };
				ID3D12GraphicsCommandList9* DynamicDepthBiasCommandList{ mIsDynamicDepthBiasSupported == true ? mCommandList9.Get() : nullptr };
				mDrawCallDispatcher.DrawDepthOnly(mCommandList.Get(), DynamicDepthBiasCommandList, ShadowDepthBias.DepthBias, ShadowDepthBias.DepthBiasClamp, ShadowDepthBias.SlopeScaledDepthBias, Data.ShadowRenderContexts[CascadeIndex], CascadeIndex, DrawCallResources.GetShadowFrameGlobalsSrvHandle(), DrawCallResources.GetShadowModelContextSrvHandle(CascadeIndex), DrawCallResources.GetShadowTerrainPatchContextSrvHandle(CascadeIndex), DrawCallResources.GetBonePaletteSrvHandle(), DrawCallResources.GetShadowDrawRecordSrvHandle(CascadeIndex), mMaterialResourceManager.GetMaterialSrvHandle(), mMaterialResourceManager.GetMaterialTextureTableSrvHandle(static_cast<std::uint32_t>(CurrentIndex)));

				ShadowDepthMap->Transition(mCommandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			}

			std::array<D3D12_CPU_DESCRIPTOR_HANDLE, GBufferTargetCount> GBufferRtvs{};
			const std::array<float, 4> GBufferAlbedoClearColor{ 0.0f, 0.0f, 0.0f, 0.0f };
			const std::array<float, 4> GBufferNormalClearColor{ 0.5f, 0.5f, 1.0f, 0.0f };
			const std::array<float, 4> GBufferWorldPositionClearColor{ 0.0f, 0.0f, 0.0f, 0.0f };
			const std::array<const float*, GBufferTargetCount> GBufferClearColors{ GBufferAlbedoClearColor.data(), GBufferNormalClearColor.data(), GBufferWorldPositionClearColor.data() };
			for (std::uint32_t GBufferIndex{ 0 }; GBufferIndex < GBufferTargetCount; GBufferIndex += 1) {
				mGBufferTargets[GBufferIndex]->Transition(mCommandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
				GBufferRtvs[GBufferIndex] = mGBufferTargets[GBufferIndex]->GetRTV();
				mCommandList->ClearRenderTargetView(GBufferRtvs[GBufferIndex], GBufferClearColors[GBufferIndex], 0, nullptr);
			}

			D3D12_CPU_DESCRIPTOR_HANDLE Dsv{ mDepthStencilBuffer->GetDSV() };
			mCommandList->ClearDepthStencilView(Dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
			mCommandList->OMSetRenderTargets(static_cast<UINT>(GBufferRtvs.size()), GBufferRtvs.data(), FALSE, &Dsv);

			mCommandList->RSSetViewports(1, &mViewport);
			mCommandList->RSSetScissorRects(1, &mScissorRect);

		
			// Execute Render Tasks
			mDrawCallDispatcher.DrawGBuffer(mCommandList.Get(), Data, DrawCallResources.GetFrameGlobalsSrvHandle(), DrawCallResources.GetModelContextSrvHandle(), DrawCallResources.GetTerrainPatchContextSrvHandle(), DrawCallResources.GetBonePaletteSrvHandle(), DrawCallResources.GetDrawRecordSrvHandle(), mMaterialResourceManager.GetMaterialSrvHandle(), mMaterialResourceManager.GetMaterialTextureTableSrvHandle(static_cast<std::uint32_t>(CurrentIndex)));

			for (std::uint32_t GBufferIndex{ 0 }; GBufferIndex < GBufferTargetCount; GBufferIndex += 1) {
				mGBufferTargets[GBufferIndex]->Transition(mCommandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			}

			TexPtr& RenderTarget{ mRenderTargets[CurrentIndex] };
			TexPtr& LightingTarget{ mLightingTargets[CurrentIndex] };
			TexPtr& PostProcessTarget{ mPostProcessTargets[CurrentIndex] };
			const bool IsPostProcessEnabled{ WidgetCore == nullptr || WidgetCore->IsPostProcessEnabled() };
			PostProcessJob ToneMappingJob{ BuildToneMappingPostProcessJob(LightingTarget, PostProcessTarget, IsPostProcessEnabled) };
			LightingTarget->Transition(mCommandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

			D3D12_CPU_DESCRIPTOR_HANDLE LightingRtv{ LightingTarget->GetRTV() };
			mCommandList->ClearRenderTargetView(LightingRtv, DirectX::Colors::Blue, 0, nullptr);
			mCommandList->OMSetRenderTargets(1, &LightingRtv, FALSE, nullptr);
			mDrawCallDispatcher.DrawDeferredLighting(mCommandList.Get(), DrawCallResources.GetFrameGlobalsSrvHandle(), DrawCallResources.GetShadowMappingParameterSrvHandle(), mShadowMapBaseSrvHandle, mGBufferTargets[GBufferAlbedoIndex]->GetSRVDescriptorHandle(), mGBufferTargets[GBufferNormalIndex]->GetSRVDescriptorHandle(), mGBufferTargets[GBufferWorldPositionIndex]->GetSRVDescriptorHandle());
			mCommandList->OMSetRenderTargets(1, &LightingRtv, FALSE, &Dsv);
			mDrawCallDispatcher.DrawSkyDome(mCommandList.Get(), Data, DrawCallResources.GetFrameGlobalsSrvHandle(), DrawCallResources.GetModelContextSrvHandle(), DrawCallResources.GetTerrainPatchContextSrvHandle(), DrawCallResources.GetBonePaletteSrvHandle(), DrawCallResources.GetDrawRecordSrvHandle(), mMaterialResourceManager.GetMaterialSrvHandle(), mMaterialResourceManager.GetMaterialTextureTableSrvHandle(static_cast<std::uint32_t>(CurrentIndex)));

			PreparePostProcessJobResources(mCommandList.Get(), ToneMappingJob);

			mCommandList->Close();
			DrawCallResources.QueueWaitForUpload(mDirectCommandQueue.Get());
			mMaterialResourceManager.QueueWaitForUpload(mDirectCommandQueue.Get(), static_cast<std::uint32_t>(CurrentIndex));
			for (const Interface::Future& TerrainUploadFuture : Data.mTerrainUploadFutures) {
				TerrainUploadFuture.QueueWait(mDirectCommandQueue.Get());
			}

			ID3D12CommandList* MainCommandLists[]{ mCommandList.Get() };
			mDirectCommandQueue->ExecuteCommandLists(_countof(MainCommandLists), MainCommandLists);

			Interface::Future SceneRenderFuture{ SignalFuture() };
			Interface::Future PostProcessFuture{ EnqueuePostProcessJob(SceneRenderFuture, ToneMappingJob) };

			BeginPostProcessFinalPass(CurrentIndex, DescriptorHeaps, PostProcessFuture);
			CopyPostProcessToBackBuffer(PostProcessTarget, RenderTarget);
			DrawFinalOverlays(Data, WidgetCore, DrawCallResources, Dsv, CurrentIndex, ShadowCascadeCount);
			FinishPresentTarget(RenderTarget, DrawCallResources, CurrentIndex);
			ExecutePostProcessFinalPass();

			ErrorHandler::report(mSwapChain->Present(Constants::AllowTearing ? 0 : 1, Constants::AllowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0), "DirectQueue", "Failed to present SwapChain.", ErrorHandler::Level::Critical);

			DirectQueue::DrainDebugMessages();

			mFrameSync.Sync(mDirectCommandQueue.Get());
			if (!Config::Query()->Get<bool>("Block_ImGui")) {
				Widget::PerformanceProvider::Get().EndFrame();
			}
        }

        void DirectQueue::InitBasements() {
            // Factory
			if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(mFactory.GetAddressOf())))) {
				ErrorHandler::report("DirectQueue", "Failed to create DXGI Factory.", ErrorHandler::Level::Critical);
            }

			// Debug Layer
#if defined(DEBUG) || defined(_DEBUG)
            if (FAILED(DXGIGetDebugInterface1(NULL, IID_PPV_ARGS(mDebugDXGI.GetAddressOf())))) {
				ErrorHandler::report("DirectQueue", "Failed to get DXGI Debug Interface.", ErrorHandler::Level::Critical);
            }

            if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(mDebugController.GetAddressOf())))) {
				ErrorHandler::report("DirectQueue", "Failed to get D3D12 Debug Interface.", ErrorHandler::Level::Critical);
            }

			mDebugController->EnableDebugLayer();
            mDebugController->SetEnableGPUBasedValidation(false);

			mDebugDXGI->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
            mDebugDXGI->EnableLeakTrackingForThread();

			if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(mDxgiInfoQueue.GetAddressOf())))) {
				mDxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
				mDxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
			}
#endif
            // Device
			ComPtr<IDXGIAdapter1> adapter = GetBestAdapter();
			mPrimaryAdapter = adapter;

			if (adapter == nullptr) {
				ErrorHandler::report("DirectQueue", "[Render] No suitable GPU found.", ErrorHandler::Level::Critical);
			}

			auto hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(mDevice.GetAddressOf()));

			if (FAILED(hr)) {
				ComPtr<IDXGIAdapter> warpAdapter{ nullptr };
				ErrorHandler::report(mFactory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)),"DirectQueue", "Falied to make WarpAdapter", ErrorHandler::Level::Critical);
				ErrorHandler::report(::D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&mDevice)), "DirectQueue", "Failed to make Warp Device", ErrorHandler::Level::Critical);
			}


			StdOutput::PrintLine("[Render] Console Test");

#if defined(DEBUG) || defined(_DEBUG)
			mDevice->QueryInterface(IID_PPV_ARGS(mD3D12InfoQueue.GetAddressOf()));
			if (mD3D12InfoQueue != nullptr) {
				mD3D12InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
				mD3D12InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
			}
#endif

			if (DirectQueue::CheckShaderModelSupport(D3D_SHADER_MODEL_6_6)) {
				StdOutput::PrintLine("[Render] Shader Model 6.6 is supported.");
			}

			D3D12_FEATURE_DATA_D3D12_OPTIONS16 Options16{};
			if (SUCCEEDED(mDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &Options16, sizeof(Options16)))) {
				mIsDynamicDepthBiasSupported = Options16.DynamicDepthBiasSupported == TRUE;
			}

			// Direct Command Queue
			D3D12_COMMAND_QUEUE_DESC queueDesc{};
			queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
			queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
			queueDesc.NodeMask = 0;

			ErrorHandler::report(mDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(mDirectCommandQueue.GetAddressOf())), "DirectQueue", "Failed to create Direct Command Queue.", ErrorHandler::Level::Critical);

			// SwapChain
			DXGI_SWAP_CHAIN_DESC1 desc{};
			desc.Width = NULL;
			desc.Height = NULL;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.Stereo = FALSE;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			desc.BufferCount = Constants::FrameCount<UINT>;
			desc.Scaling = DXGI_SCALING_STRETCH;
			desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
			desc.Flags = Constants::AllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

			ErrorHandler::report(mFactory->CreateSwapChainForHwnd(mDirectCommandQueue.Get(), mHwnd, &desc, nullptr, nullptr, mSwapChain.GetAddressOf()), "DirectQueue", "Failed to create SwapChain.", ErrorHandler::Level::Critical);
        }

		void DirectQueue::InitWorkers() {
			mFrameSync = FrameSync(mDevice.Get());
			ErrorHandler::report(mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(mDirectFence.GetAddressOf())), "DirectQueue", "Failed to create direct queue fence.", ErrorHandler::Level::Critical);
			mDirectFenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
			ErrorHandler::report(mDirectFenceEvent == nullptr, "DirectQueue", "Failed to create direct queue fence event.", ErrorHandler::Level::Critical);
		}

		void DirectQueue::InitCommandList() {
			for (auto& allocator : mMainCommandAllocators) {
				ErrorHandler::report(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.GetAddressOf())), "DirectQueue", "Failed to create Main Command Allocator.", ErrorHandler::Level::Critical);
			}
			ErrorHandler::report(mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mMainCommandAllocators[0].Get(), nullptr, IID_PPV_ARGS(mCommandList.GetAddressOf())), "DirectQueue", "Failed to create Main Command List.", ErrorHandler::Level::Critical);
			if (mIsDynamicDepthBiasSupported == true) {
				mCommandList->QueryInterface(IID_PPV_ARGS(mCommandList9.GetAddressOf()));
				mIsDynamicDepthBiasSupported = mCommandList9 != nullptr;
			}

			mCommandList->Close();

			for (ComPtr<ID3D12CommandAllocator>& PostProcessCommandAllocator : mPostProcessCommandAllocators) {
				ErrorHandler::report(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(PostProcessCommandAllocator.GetAddressOf())), "DirectQueue", "Failed to create post process command allocator.", ErrorHandler::Level::Critical);
			}

			ErrorHandler::report(mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mPostProcessCommandAllocators[0].Get(), nullptr, IID_PPV_ARGS(mPostProcessCommandList.GetAddressOf())), "DirectQueue", "Failed to create post process command list.", ErrorHandler::Level::Critical);
			mPostProcessCommandList->Close();
		}

		void DirectQueue::InitTargetResources() {
			mRTVHeap = DescriptorHeap(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, (Constants::FrameCount<std::uint32_t> * 3u) + GBufferTargetCount, false);
			mSrvHeap = DescriptorHeap(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 512, true);
			for (std::size_t ShadowCascadeIndex{ 0 }; ShadowCascadeIndex < Game::RFD::ShadowCascadeMaxCount; ShadowCascadeIndex += 1) {
				mShadowMapSrvHandles[ShadowCascadeIndex] = mSrvHeap.Allocate();
			}

			mShadowMapBaseSrvHandle = mShadowMapSrvHandles[0];
			mShadowDSVHeap = DescriptorHeap(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, Game::RFD::ShadowCascadeMaxCount, false);

			for (auto&& [i, rt] : views::enumerate(mRenderTargets)) {
				ComPtr<ID3D12Resource> backBuffer{ nullptr };
				ErrorHandler::report(mSwapChain->GetBuffer(static_cast<UINT>(i), IID_PPV_ARGS(backBuffer.GetAddressOf())), "DirectQueue", "Failed to get SwapChain BackBuffer.", ErrorHandler::Level::Critical);
				rt = Texture::CreateFromResource(backBuffer.Get(), "BackBuffer_" + std::to_string(i));

				rt->CreateRTV(mDevice.Get(), &mRTVHeap);
			}

			const std::uint32_t Width{ Config::Query()->Get<uint32_t>("Window_Width") };
			const std::uint32_t Height{ Config::Query()->Get<uint32_t>("Window_Height") };
			const float LightingClearColor[4]{ 0.0f, 0.0f, 1.0f, 1.0f };
			CD3DX12_CLEAR_VALUE LightingOptimizedClearValue{ DXGI_FORMAT_R16G16B16A16_FLOAT, LightingClearColor };
			for (TexPtr& LightingTarget : mLightingTargets) {
				LightingTarget = Texture::CreateTarget(mDevice.Get(), Width, Height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureUsage::RenderTarget, &LightingOptimizedClearValue);
				LightingTarget->CreateRTV(mDevice.Get(), &mRTVHeap);
				LightingTarget->CreateSRV(mDevice.Get(), &mSrvHeap);
			}

			for (TexPtr& PostProcessTarget : mPostProcessTargets) {
				PostProcessTarget = Texture::CreateTarget(mDevice.Get(), Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM, TextureUsage::RenderTargetUnorderedAccess);
				PostProcessTarget->CreateRTV(mDevice.Get(), &mRTVHeap);
				PostProcessTarget->CreateUAV(mDevice.Get(), &mSrvHeap);
			}

			for (std::size_t Index{ 0 }; Index < Constants::FrameCount<std::size_t>; ++Index) {
				mDrawCallResourceManagers[Index].Initialize(mDevice.Get(), &mSrvHeap, static_cast<std::uint32_t>(Index));
			}

			mMaterialResourceManager.Initialize(mDevice.Get(), &mSrvHeap);

			mDSVHeap = DescriptorHeap(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

			CD3DX12_CLEAR_VALUE depthOptimizedClearValue{ DXGI_FORMAT_D24_UNORM_S8_UINT, 1.0f, 0 };
			mDepthStencilBuffer = Texture::CreateTarget(mDevice.Get(), Config::Query()->Get<uint32_t>("Window_Width"), Config::Query()->Get<uint32_t>("Window_Height"), DXGI_FORMAT_D24_UNORM_S8_UINT, TextureUsage::DepthStencil, &depthOptimizedClearValue);

			mDepthStencilBuffer->CreateDSV(mDevice.Get(), &mDSVHeap);
			DirectQueue::InitGBufferResources();
		}

		void DirectQueue::InitGBufferResources() {
			const std::uint32_t Width{ Config::Query()->Get<uint32_t>("Window_Width") };
			const std::uint32_t Height{ Config::Query()->Get<uint32_t>("Window_Height") };
			const std::array<DXGI_FORMAT, GBufferTargetCount> Formats{ DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT };
			const std::array<DirectX::XMFLOAT4, GBufferTargetCount> ClearColors{ DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 0.0f }, DirectX::XMFLOAT4{ 0.5f, 0.5f, 1.0f, 0.0f }, DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 0.0f } };

			for (std::uint32_t GBufferIndex{ 0 }; GBufferIndex < GBufferTargetCount; GBufferIndex += 1) {
				const DirectX::XMFLOAT4& ClearColor{ ClearColors[GBufferIndex] };
				const float ClearColorValues[4]{ ClearColor.x, ClearColor.y, ClearColor.z, ClearColor.w };
				CD3DX12_CLEAR_VALUE OptimizedClearValue{ Formats[GBufferIndex], ClearColorValues };
				mGBufferTargets[GBufferIndex] = Texture::CreateTarget(mDevice.Get(), Width, Height, Formats[GBufferIndex], TextureUsage::RenderTarget, &OptimizedClearValue);
				mGBufferTargets[GBufferIndex]->CreateRTV(mDevice.Get(), &mRTVHeap);
				mGBufferTargets[GBufferIndex]->CreateSRV(mDevice.Get(), &mSrvHeap);
			}
		}

		void DirectQueue::EnsureShadowMapResources(const Game::RFD::ShadowMappingParameter& ShadowMappingParameter) {
			const uint32_t RequiredShadowCascadeCount{ std::max<uint32_t>(1u, std::min<uint32_t>(ShadowMappingParameter.cascadeCount, Game::RFD::ShadowCascadeMaxCount)) };
			std::array<uint32_t, Game::RFD::ShadowCascadeMaxCount> RequiredShadowMapSizes{};
			for (uint32_t ShadowCascadeIndex{ 0 }; ShadowCascadeIndex < RequiredShadowCascadeCount; ShadowCascadeIndex += 1) {
				const float ShadowMapSizeFloat{ std::max(ShadowMappingParameter.minimumShadowMapSize, ShadowMappingParameter.shadowMapSizes[ShadowCascadeIndex]) };
				const uint64_t ShadowMapSize{ static_cast<uint64_t>(ShadowMapSizeFloat) };
				const uint64_t ClampedShadowMapSize{ std::min<uint64_t>(ShadowMapSize, static_cast<uint64_t>(D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION)) };
				RequiredShadowMapSizes[ShadowCascadeIndex] = static_cast<uint32_t>(ClampedShadowMapSize);
			}

			bool IsAllShadowDepthMapsValid{ true };
			for (uint32_t ShadowCascadeIndex{ 0 }; ShadowCascadeIndex < RequiredShadowCascadeCount; ShadowCascadeIndex += 1) {
				if (mShadowDepthMaps[ShadowCascadeIndex] == nullptr) {
					IsAllShadowDepthMapsValid = false;
					break;
				}
			}

			bool IsSameShadowMapSizes{ true };
			for (uint32_t ShadowCascadeIndex{ 0 }; ShadowCascadeIndex < RequiredShadowCascadeCount; ShadowCascadeIndex += 1) {
				if (mShadowMapSizes[ShadowCascadeIndex] != RequiredShadowMapSizes[ShadowCascadeIndex]) {
					IsSameShadowMapSizes = false;
					break;
				}
			}

			if (IsAllShadowDepthMapsValid == true && IsSameShadowMapSizes == true && mShadowCascadeCount == RequiredShadowCascadeCount) {
				return;
			}

			mShadowCascadeCount = RequiredShadowCascadeCount;
			mShadowMapSizes = {};
			mShadowDSVHeap = DescriptorHeap(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, RequiredShadowCascadeCount, false);

			for (std::size_t ShadowCascadeIndex{ 0 }; ShadowCascadeIndex < Game::RFD::ShadowCascadeMaxCount; ShadowCascadeIndex += 1) {
				mShadowDepthMaps[ShadowCascadeIndex].reset();
			}

			CD3DX12_CLEAR_VALUE depthOptimizedClearValue{ DXGI_FORMAT_D24_UNORM_S8_UINT, 1.0f, 0 };
			for (uint32_t ShadowCascadeIndex{ 0 }; ShadowCascadeIndex < RequiredShadowCascadeCount; ShadowCascadeIndex += 1) {
				const uint32_t CurrentShadowMapSize{ RequiredShadowMapSizes[ShadowCascadeIndex] };
				mShadowMapSizes[ShadowCascadeIndex] = CurrentShadowMapSize;
				mShadowDepthMaps[ShadowCascadeIndex] = Texture::CreateTarget(mDevice.Get(), CurrentShadowMapSize, CurrentShadowMapSize, DXGI_FORMAT_R24G8_TYPELESS, TextureUsage::DepthStencil, &depthOptimizedClearValue);
				mShadowDepthMaps[ShadowCascadeIndex]->CreateDSV(mDevice.Get(), &mShadowDSVHeap);
				D3D12_SHADER_RESOURCE_VIEW_DESC ShadowMapSrvDescription{};
				ShadowMapSrvDescription.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
				ShadowMapSrvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				ShadowMapSrvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				ShadowMapSrvDescription.Texture2D.MipLevels = 1;
				mDevice->CreateShaderResourceView(mShadowDepthMaps[ShadowCascadeIndex]->GetResource(), &ShadowMapSrvDescription, mShadowMapSrvHandles[ShadowCascadeIndex].GetCPU());
				mShadowViewports[ShadowCascadeIndex] = D3D12_VIEWPORT{ 0.0f, 0.0f, static_cast<float>(CurrentShadowMapSize), static_cast<float>(CurrentShadowMapSize), 0.0f, 1.0f };
				mShadowScissorRects[ShadowCascadeIndex] = D3D12_RECT{ 0, 0, static_cast<LONG>(CurrentShadowMapSize), static_cast<LONG>(CurrentShadowMapSize) };
			}
		}

		PostProcessJob DirectQueue::BuildToneMappingPostProcessJob(const TexPtr& SourceTarget, const TexPtr& DestinationTarget, bool IsPostProcessEnabled) {
			PostProcessJob Job{};
			Job.mPipelineName = "ToneMappingCompute";
			Job.mSourceTarget = SourceTarget;
			Job.mDestinationTarget = DestinationTarget;
			Job.mRootConstants.mParameter0 = std::bit_cast<std::uint32_t>(0.7f);
			Job.mRootConstants.mParameter1 = std::bit_cast<std::uint32_t>(2.2f);
			Job.mRootConstants.mParameter2 = IsPostProcessEnabled == true ? 1u : 0u;
			Job.mThreadGroupSizeX = 8u;
			Job.mThreadGroupSizeY = 8u;
			Job.mThreadGroupSizeZ = 1u;
			return Job;
		}

		void DirectQueue::PreparePostProcessJobResources(ID3D12GraphicsCommandList* CommandList, const PostProcessJob& Job) {
			if (CommandList == nullptr || Job.mSourceTarget == nullptr || Job.mDestinationTarget == nullptr) {
				return;
			}

			Job.mSourceTarget->Transition(CommandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			Job.mDestinationTarget->Transition(CommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}

		Interface::Future DirectQueue::EnqueuePostProcessJob(const Interface::Future& WaitFuture, const PostProcessJob& Job) {
			if (mComputeQueue == nullptr || Job.mSourceTarget == nullptr || Job.mDestinationTarget == nullptr) {
				return Interface::Future{};
			}

			Game::Base::Pipeline* Pipeline{ ResolvePostProcessPipeline(Job.mPipelineName) };
			ErrorHandler::report(Pipeline == nullptr, "DirectQueue", "Failed to initialize post process pipeline.", ErrorHandler::Level::Critical);

			PostProcessRootConstants RootConstants{ Job.mRootConstants };
			RootConstants.mSourceSrvIndex = Job.mSourceTarget->GetSRVDescriptorHandle().GetIndex();
			RootConstants.mDestinationUavIndex = Job.mDestinationTarget->GetUAVDescriptorHandle().GetIndex();
			RootConstants.mTargetWidth = Job.mDestinationTarget->GetWidth();
			RootConstants.mTargetHeight = Job.mDestinationTarget->GetHeight();

			Interface::ComputeQueueDispatchRequest PostProcessRequest{};
			PostProcessRequest.WaitFuture = WaitFuture;
			PostProcessRequest.RootSignature = Pipeline->GetRootSignature();
			PostProcessRequest.PipelineState = Pipeline->Get();
			PostProcessRequest.DescriptorHeaps = std::vector<ID3D12DescriptorHeap*>{ mSrvHeap.GetHeap() };
			PostProcessRequest.RecordCommands = [RootConstants](ID3D12GraphicsCommandList* CommandList) {
				if (CommandList == nullptr) {
					return;
				}

				CommandList->SetComputeRoot32BitConstants(0, static_cast<UINT>(sizeof(PostProcessRootConstants) / sizeof(std::uint32_t)), &RootConstants, 0);
			};
			const std::uint32_t ThreadGroupSizeX{ std::max<std::uint32_t>(1u, Job.mThreadGroupSizeX) };
			const std::uint32_t ThreadGroupSizeY{ std::max<std::uint32_t>(1u, Job.mThreadGroupSizeY) };
			PostProcessRequest.ThreadGroupCountX = DivideRoundUp(Job.mDestinationTarget->GetWidth(), ThreadGroupSizeX);
			PostProcessRequest.ThreadGroupCountY = DivideRoundUp(Job.mDestinationTarget->GetHeight(), ThreadGroupSizeY);
			PostProcessRequest.ThreadGroupCountZ = std::max<std::uint32_t>(1u, Job.mThreadGroupSizeZ);

			Interface::Future PostProcessFuture{ mComputeQueue->EnqueueComputeFuture(PostProcessRequest) };
			mComputeQueue->DispatchComputes();
			return PostProcessFuture;
		}

		Game::Base::Pipeline* DirectQueue::ResolvePostProcessPipeline(const std::string& PipelineName) {
			if (PipelineName.empty() == true) {
				return nullptr;
			}

			std::unordered_map<std::string, Game::Base::Pipeline>::iterator FoundPipeline{ mPostProcessPipelines.find(PipelineName) };
			if (FoundPipeline != mPostProcessPipelines.end()) {
				return &FoundPipeline->second;
			}

			Game::Base::Pipeline NewPipeline{};
			if (NewPipeline.Initialize(PipelineName) == false) {
				return nullptr;
			}

			std::pair<std::unordered_map<std::string, Game::Base::Pipeline>::iterator, bool> InsertResult{ mPostProcessPipelines.emplace(PipelineName, std::move(NewPipeline)) };
			if (InsertResult.second == false) {
				return nullptr;
			}

			return &InsertResult.first->second;
		}

		void DirectQueue::BeginPostProcessFinalPass(std::uint32_t CurrentIndex, const std::array<ID3D12DescriptorHeap*, 1>& DescriptorHeaps, const Interface::Future& PostProcessFuture) {
			ComPtr<ID3D12CommandAllocator>& PostProcessCommandAllocator{ mPostProcessCommandAllocators[CurrentIndex] };
			PostProcessCommandAllocator->Reset();
			mPostProcessCommandList->Reset(PostProcessCommandAllocator.Get(), nullptr);
			mPostProcessCommandList->SetDescriptorHeaps(static_cast<UINT>(DescriptorHeaps.size()), DescriptorHeaps.data());
			QueueWaitFuture(PostProcessFuture);
		}

		void DirectQueue::CopyPostProcessToBackBuffer(const TexPtr& PostProcessTarget, const TexPtr& RenderTarget) {
			if (PostProcessTarget == nullptr || RenderTarget == nullptr) {
				return;
			}

			PostProcessTarget->Transition(mPostProcessCommandList.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
			RenderTarget->Transition(mPostProcessCommandList.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
			mPostProcessCommandList->CopyResource(RenderTarget->GetResource(), PostProcessTarget->GetResource());
		}

		void DirectQueue::DrawFinalOverlays(Game::RFD::RenderFrameData& Data, Widget::WidgetCore* WidgetCore, DrawCallResourceManager& DrawCallResources, D3D12_CPU_DESCRIPTOR_HANDLE Dsv, std::uint32_t CurrentIndex, std::uint32_t ShadowCascadeCount) {
			TexPtr& RenderTarget{ mRenderTargets[CurrentIndex] };
			RenderTarget->Transition(mPostProcessCommandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
			mPostProcessCommandList->RSSetViewports(1, &mViewport);
			mPostProcessCommandList->RSSetScissorRects(1, &mScissorRect);

			D3D12_CPU_DESCRIPTOR_HANDLE Rtv{ RenderTarget->GetRTV() };
			mPostProcessCommandList->OMSetRenderTargets(1, &Rtv, FALSE, &Dsv);
			mDrawCallDispatcher.DrawForwardOverlays(mPostProcessCommandList.Get(), Data, DrawCallResources.GetFrameGlobalsSrvHandle(), DrawCallResources.GetModelContextSrvHandle(), DrawCallResources.GetBoundingBoxContextSrvHandle(), DrawCallResources.GetDebugGeometryContextSrvHandle(), DrawCallResources.GetTerrainPatchContextSrvHandle(), DrawCallResources.GetBonePaletteSrvHandle(), DrawCallResources.GetDrawRecordSrvHandle(), mMaterialResourceManager.GetMaterialSrvHandle(), mMaterialResourceManager.GetMaterialTextureTableSrvHandle(static_cast<std::uint32_t>(CurrentIndex)));
			if (WidgetCore != nullptr) {
#pragma region TemporaryShadowMapPreview
				std::array<ID3D12Resource*, Widget::WidgetCore::ShadowMapPreviewCapacity> ShadowMapResources{};
				const std::uint32_t ShadowMapPreviewCount{ std::min<std::uint32_t>(ShadowCascadeCount, Widget::WidgetCore::ShadowMapPreviewCapacity) };
				for (std::uint32_t ShadowMapIndex{ 0 }; ShadowMapIndex < ShadowMapPreviewCount; ShadowMapIndex += 1) {
					if (mShadowDepthMaps[ShadowMapIndex] == nullptr) {
						continue;
					}

					ShadowMapResources[ShadowMapIndex] = mShadowDepthMaps[ShadowMapIndex]->GetResource();
				}

				WidgetCore->SetShadowMapTextures(ShadowMapResources, ShadowMapPreviewCount, mShadowMapSizes[0]);
#pragma endregion
				WidgetCore->Render(mPostProcessCommandList);
			}
		}

		void DirectQueue::FinishPresentTarget(const TexPtr& RenderTarget, DrawCallResourceManager& DrawCallResources, std::uint32_t CurrentIndex) {
			if (RenderTarget == nullptr) {
				return;
			}

			DrawCallResources.TransitionToCopyDestination(mPostProcessCommandList.Get());
			mMaterialResourceManager.TransitionToCopyDestination(mPostProcessCommandList.Get(), static_cast<std::uint32_t>(CurrentIndex));
			RenderTarget->Transition(mPostProcessCommandList.Get(), D3D12_RESOURCE_STATE_PRESENT);
		}

		void DirectQueue::ExecutePostProcessFinalPass() {
			mPostProcessCommandList->Close();
			ID3D12CommandList* PostProcessCommandLists[]{ mPostProcessCommandList.Get() };
			mDirectCommandQueue->ExecuteCommandLists(_countof(PostProcessCommandLists), PostProcessCommandLists);
		}

		ComPtr<IDXGIAdapter1> DirectQueue::GetBestAdapter() {
			StdOutput::PrintLine("[Render] ====================Selecting Adapter====================");

			ComPtr<IDXGIAdapter1> bestAdapter;
			size_t maxVRAM = 0;

			for (UINT i = 0; ; i++) {
				ComPtr<IDXGIAdapter1> adapter;
				if (mFactory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
					break;
				}

				DXGI_ADAPTER_DESC1 desc;
				adapter->GetDesc1(&desc);

				if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

				
				StdOutput::PrintLine("[Render] Adapter{:^3} : {} | VRAM: {} MB", i, ConvertWstringToUtf8(desc.Description), desc.DedicatedVideoMemory / (1024 * 1024));

				if (desc.DedicatedVideoMemory > maxVRAM) {
					maxVRAM = desc.DedicatedVideoMemory;
					bestAdapter = adapter;
				}
			}

			if (bestAdapter) {
				DXGI_ADAPTER_DESC1 bestDesc;
				bestAdapter->GetDesc1(&bestDesc);
				StdOutput::PrintLine("[Render] Selected Adapter: {} | VRAM: {} MB", ConvertWstringToUtf8(bestDesc.Description), bestDesc.DedicatedVideoMemory / (1024 * 1024));
			}
			else {
				StdOutput::PrintLine("[Render] No suitable GPU found.");
			}
			StdOutput::PrintLine("[Render] =========================================================");

			return bestAdapter;
        }

		void DirectQueue::DrainDebugMessages() {
#if defined(DEBUG) || defined(_DEBUG)
			std::ofstream logFile{ "DxDebugLayer.log", std::ios::app };

			if (mDxgiInfoQueue != nullptr) {
				const UINT64 dxgiMessageCount{ mDxgiInfoQueue->GetNumStoredMessagesAllowedByRetrievalFilters(DXGI_DEBUG_ALL) };
				for (UINT64 index{ 0 }; index < dxgiMessageCount; ++index) {
					SIZE_T messageLength{};
					if (FAILED(mDxgiInfoQueue->GetMessage(DXGI_DEBUG_ALL, index, nullptr, &messageLength))) {
						continue;
					}

					std::vector<unsigned char> messageBuffer{};
					messageBuffer.resize(messageLength);
					DXGI_INFO_QUEUE_MESSAGE* message{ reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(messageBuffer.data()) };
					if (FAILED(mDxgiInfoQueue->GetMessage(DXGI_DEBUG_ALL, index, message, &messageLength))) {
						continue;
					}

					if (message->pDescription == nullptr) {
						continue;
					}

					StdOutput::PrintLine("[Render] [DXGI] {}", message->pDescription);
					if (logFile.is_open()) {
						logFile << "DXGI: " << message->pDescription << std::endl;
					}
				}
				mDxgiInfoQueue->ClearStoredMessages(DXGI_DEBUG_ALL);
			}

			if (mD3D12InfoQueue != nullptr) {
				const UINT64 d3d12MessageCount{ mD3D12InfoQueue->GetNumStoredMessagesAllowedByRetrievalFilter() };
				for (UINT64 index{ 0 }; index < d3d12MessageCount; ++index) {
					SIZE_T messageLength{};
					if (FAILED(mD3D12InfoQueue->GetMessage(index, nullptr, &messageLength))) {
						continue;
					}

					std::vector<unsigned char> messageBuffer{};
					messageBuffer.resize(messageLength);
					D3D12_MESSAGE* message{ reinterpret_cast<D3D12_MESSAGE*>(messageBuffer.data()) };
					if (FAILED(mD3D12InfoQueue->GetMessage(index, message, &messageLength))) {
						continue;
					}

					if (message->pDescription == nullptr) {
						continue;
					}

					StdOutput::PrintLine("[Render] [D3D12] {}", message->pDescription);
					if (logFile.is_open()) {
						logFile << "D3D12: " << message->pDescription << std::endl;
					}
				}
				mD3D12InfoQueue->ClearStoredMessages();
			}
#endif
		}

		bool DirectQueue::CheckShaderModelSupport(D3D_SHADER_MODEL targetModel) {
			D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{ targetModel };

			if (SUCCEEDED(mDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel)))) {
				return (shaderModel.HighestShaderModel >= targetModel);
			}

			return false;
		}

    }
}
