#include "DirectQueue.h"
#include "Utility/ErrorHandler.h"
#include "Utility/Views.h"
#include "Core/Config.h"

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
            for (auto& worker : mRenderWorkers) {
                worker.RequestStop(); 
            }
        }

        void DirectQueue::Update() {

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
#endif 
            // Device
			ComPtr<IDXGIAdapter1> adapter = GetBestAdapter();

			if (adapter == nullptr) {
				ErrorHandler::report("DirectQueue", "No suitable GPU found.", ErrorHandler::Level::Critical);
			}

			auto hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(mDevice.GetAddressOf()));

			if (FAILED(hr)) {
				ComPtr<IDXGIAdapter> warpAdapter{ nullptr };
				ErrorHandler::report(mFactory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)),"DirectQueue", "Falied to make WarpAdapter", ErrorHandler::Level::Critical);
				ErrorHandler::report(::D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&mDevice)), "DirectQueue", "Failed to make Warp Device", ErrorHandler::Level::Critical);
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

			mRenderWorkers.Init(std::thread::hardware_concurrency() - 2, mRenderContext);
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

			for (auto&& [i, rt] : views::enumerate(mRenderTargets)) {
				ComPtr<ID3D12Resource> backBuffer{ nullptr };
				ErrorHandler::report(mSwapChain->GetBuffer(static_cast<UINT>(i), IID_PPV_ARGS(backBuffer.GetAddressOf())), "DirectQueue", "Failed to get SwapChain BackBuffer.", ErrorHandler::Level::Critical);
				rt = Texture::CreateFromResource(backBuffer.Get(), "BackBuffer_" + std::to_string(i));

				DescriptorHandle handle{ mRTVHeap.Allocate() };
				rt->CreateRTV(mDevice.Get(), handle);
			}



			mDSVHeap = DescriptorHeap(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

			CD3DX12_CLEAR_VALUE depthOptimizedClearValue{ DXGI_FORMAT_D24_UNORM_S8_UINT, 0.0f, 0 };
			mDepthStencilBuffer = Texture::CreateTarget(mDevice.Get(), Config::Query().Get<uint32_t>("Window_Width"), Config::Query().Get<uint32_t>("Window_Height"), DXGI_FORMAT_D24_UNORM_S8_UINT, TextureUsage::DepthStencil, &depthOptimizedClearValue);

			DescriptorHandle handle{ mDSVHeap.Allocate() };
			mDepthStencilBuffer->CreateDSV(mDevice.Get(), handle);
		}

        ComPtr<IDXGIAdapter1> DirectQueue::GetBestAdapter() {
			OutputDebugString(L"\n\n====================Selecting Adapter====================\n\n");

			ComPtr<IDXGIAdapter1> bestAdapter;
			size_t maxVRAM = 0;

			std::wstring message{};

			for (UINT i = 0; ; i++) {
				ComPtr<IDXGIAdapter1> adapter;
				if (mFactory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
					break;
				}

				DXGI_ADAPTER_DESC1 desc;
				adapter->GetDesc1(&desc);

				if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
				message = std::format(L"Adapter{:^3} : {} | VRAM: {} MB\n", i, desc.Description, desc.DedicatedVideoMemory / (1024 * 1024));
				OutputDebugString(message.c_str());

				if (desc.DedicatedVideoMemory > maxVRAM) {
					maxVRAM = desc.DedicatedVideoMemory;
					bestAdapter = adapter;
				}
			}

			if (bestAdapter) {
				DXGI_ADAPTER_DESC1 bestDesc;
				bestAdapter->GetDesc1(&bestDesc);

				message = std::format(L"Selected Adapter: {} | VRAM: {} MB\n", bestDesc.Description, bestDesc.DedicatedVideoMemory / (1024 * 1024));
				OutputDebugString(message.c_str());
			}
			else {
				OutputDebugStringA("No suitable GPU found.\n");
			}
			OutputDebugString(L"\n=========================================================\n\n");

			return bestAdapter;
        }
    }
}

