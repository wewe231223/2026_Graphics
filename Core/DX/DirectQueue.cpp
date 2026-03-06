#include "DirectQueue.h"
#include "Utility/ErrorHandler.h"
#include "Utility/Views.h"
#include "Utility/StringUtils.h"
#include "Utility/StdOutput.h"
#include "Core/Config.h"
#include <fstream>
#include "Widget/PerformanceProvider.h"


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

        }

		ID3D12Device* DirectQueue::GetDevice() const {
			return mDevice.Get();
		}

		DescriptorHeap* DirectQueue::GetSrvHeap() {
			return &mSrvHeap;
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


		void DirectQueue::SetCurrentSceneName(const std::string& SceneName) {
			mWidgetCore.SetCurrentSceneName(SceneName);
		}

		void DirectQueue::Render(Game::RFD::RenderFrameData& Data) {
			ErrorHandler::report(mCopyQueue == nullptr, "DirectQueue", "CopyQueue is not set.", ErrorHandler::Level::Critical);

			auto currentIndex = mFrameSync.GetCurrentIndex();

			auto& allocator = mMainCommandAllocators[currentIndex];
			allocator->Reset();
			mCommandList->Reset(allocator.Get(), nullptr);

			std::array<ID3D12DescriptorHeap*, 1> DescriptorHeaps{ mSrvHeap.GetHeap() };
			mCommandList->SetDescriptorHeaps(static_cast<UINT>(DescriptorHeaps.size()), DescriptorHeaps.data());

			auto& rt = mRenderTargets[currentIndex];
			rt->Transition(mCommandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

			DrawCallResourceManager& DrawCallResources{ mDrawCallResourceManagers[currentIndex] };
			mMaterialResourceManager.TransitionToShaderResource(mCommandList.Get(), static_cast<uint32_t>(currentIndex));
			DrawCallResources.TransitionToShaderResource(mCommandList.Get());

			auto rtv = rt->GetRTV();
			auto dsv = mDepthStencilBuffer->GetDSV();

			mCommandList->ClearRenderTargetView(rtv, DirectX::Colors::Blue, 0, nullptr);


			mCommandList->ClearDepthStencilView(mDepthStencilBuffer->GetDSV(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
			mCommandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

			mCommandList->RSSetViewports(1, &mViewport);
			mCommandList->RSSetScissorRects(1, &mScissorRect);

		
			// Execute Render Tasks
			mDrawCallDispatcher.DrawForward(mCommandList.Get(), Data, DrawCallResources.GetFrameGlobalsSrvHandle(), DrawCallResources.GetModelContextSrvHandle(), DrawCallResources.GetDrawRecordSrvHandle(), mMaterialResourceManager.GetMaterialSrvHandle(), mMaterialResourceManager.GetMaterialTextureTableSrvHandle(static_cast<uint32_t>(currentIndex)));
			Widget::PerformanceProvider::Get().EndProfile();
			mWidgetCore.Render(mCommandList);



			DrawCallResources.TransitionToCopyDestination(mCommandList.Get());
			mMaterialResourceManager.TransitionToCopyDestination(mCommandList.Get(), static_cast<uint32_t>(currentIndex));

			rt->Transition(mCommandList.Get(), D3D12_RESOURCE_STATE_PRESENT);



			mCommandList->Close();
			DrawCallResources.WaitForUpload(mCopyQueue);
			mMaterialResourceManager.WaitForUpload(mCopyQueue, static_cast<uint32_t>(currentIndex));


			ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
			mDirectCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

			ErrorHandler::report(mSwapChain->Present(Constants::AllowTearing ? 0 : 1, Constants::AllowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0), "DirectQueue", "Failed to present SwapChain.", ErrorHandler::Level::Critical);
			Widget::PerformanceProvider::Get().EndFrame();

			DirectQueue::DrainDebugMessages();

			mFrameSync.Sync(mDirectCommandQueue.Get());
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
            mDebugController->SetEnableGPUBasedValidation(true);

			mDebugDXGI->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
            mDebugDXGI->EnableLeakTrackingForThread();

			if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(mDxgiInfoQueue.GetAddressOf())))) {
				mDxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
				mDxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
			}
#endif
            // Device
			ComPtr<IDXGIAdapter1> adapter = GetBestAdapter();

			if (adapter == nullptr) {
				ErrorHandler::report("DirectQueue", "[Render] No suitable GPU found.", ErrorHandler::Level::Critical);
			}

			auto hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(mDevice.GetAddressOf()));

			if (FAILED(hr)) {
				ComPtr<IDXGIAdapter> warpAdapter{ nullptr };
				ErrorHandler::report(mFactory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)),"DirectQueue", "Falied to make WarpAdapter", ErrorHandler::Level::Critical);
				ErrorHandler::report(::D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&mDevice)), "DirectQueue", "Failed to make Warp Device", ErrorHandler::Level::Critical);
			}

			mWidgetCore.Initialize(mHwnd, mDevice, adapter.Get());

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
		}

		void DirectQueue::InitCommandList() {
			for (auto& allocator : mMainCommandAllocators) {
				ErrorHandler::report(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.GetAddressOf())), "DirectQueue", "Failed to create Main Command Allocator.", ErrorHandler::Level::Critical);
			}
			ErrorHandler::report(mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mMainCommandAllocators[0].Get(), nullptr, IID_PPV_ARGS(mCommandList.GetAddressOf())), "DirectQueue", "Failed to create Main Command List.", ErrorHandler::Level::Critical);

			mCommandList->Close();
		}

		void DirectQueue::InitTargetResources() {
			mRTVHeap = DescriptorHeap(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, Constants::FrameCount<uint32_t>, false);
			mSrvHeap = DescriptorHeap(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 512, true);

			for (auto&& [i, rt] : views::enumerate(mRenderTargets)) {
				ComPtr<ID3D12Resource> backBuffer{ nullptr };
				ErrorHandler::report(mSwapChain->GetBuffer(static_cast<UINT>(i), IID_PPV_ARGS(backBuffer.GetAddressOf())), "DirectQueue", "Failed to get SwapChain BackBuffer.", ErrorHandler::Level::Critical);
				rt = Texture::CreateFromResource(backBuffer.Get(), "BackBuffer_" + std::to_string(i));

				rt->CreateRTV(mDevice.Get(), &mRTVHeap);
			}

			for (std::size_t Index{ 0 }; Index < Constants::FrameCount<std::size_t>; ++Index) {
				mDrawCallResourceManagers[Index].Initialize(mDevice.Get(), &mSrvHeap, static_cast<std::uint32_t>(Index));
			}

			mMaterialResourceManager.Initialize(mDevice.Get(), &mSrvHeap);

			mDSVHeap = DescriptorHeap(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

			CD3DX12_CLEAR_VALUE depthOptimizedClearValue{ DXGI_FORMAT_D24_UNORM_S8_UINT, 1.0f, 0 };
			mDepthStencilBuffer = Texture::CreateTarget(mDevice.Get(), Config::Query()->Get<uint32_t>("Window_Width"), Config::Query()->Get<uint32_t>("Window_Height"), DXGI_FORMAT_D24_UNORM_S8_UINT, TextureUsage::DepthStencil, &depthOptimizedClearValue);

			mDepthStencilBuffer->CreateDSV(mDevice.Get(), &mDSVHeap);
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
