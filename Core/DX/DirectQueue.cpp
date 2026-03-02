#include "DirectQueue.h"
#include "Utility/ErrorHandler.h"
#include "Utility/Views.h"
#include "Utility/StringUtils.h"
#include "Utility/StdOutput.h"
#include "Core/Config.h"
#include <algorithm>
#include <fstream>
#include <vector>


namespace Core {
    namespace DX {
		struct DrawRootConstantsB1 {
			uint32_t FrameGlobalsSrvIndex{ 0 };
			uint32_t ModelContextSrvIndex{ 0 };
			uint32_t DrawRecordSrvIndex{ 0 };
			uint32_t DrawRecordBaseIndex{ 0 };
			float TintColor[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
			uint32_t Reserved0{ 0 };
			uint32_t Reserved1{ 0 };
			uint32_t Reserved2{ 0 };
			uint32_t Reserved3{ 0 };
		};

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

		void DirectQueue::SetUploadInfrastructure(GraphicsAllocator* GraphicsAllocator, Interface::ICopyQueue* CopyQueue) {
			mGraphicsAllocator = GraphicsAllocator;
			mCopyQueue = CopyQueue;
		}

		void DirectQueue::PreRender(Game::RFD::RenderFrameData& Data, float Dt) {
			Data.globals.dt = Dt;
			mRTVIndex = static_cast<uint32_t>(mFrameSync.GetCurrentIndex());

			ErrorHandler::report(mGraphicsAllocator == nullptr, "DirectQueue", "GraphicsAllocator is not set.", ErrorHandler::Level::Critical);
			ErrorHandler::report(mCopyQueue == nullptr, "DirectQueue", "CopyQueue is not set.", ErrorHandler::Level::Critical);

			std::stable_sort(Data.drawRecords.begin(), Data.drawRecords.end(), DirectQueue::CompareDrawRecordByPso);

			DirectQueue::BuildDrawRecordGpu(Data);

			GraphicsVector& ModelContextVector = mPerFrameModelContextVectors[mRTVIndex];
			GraphicsVector& DrawRecordVector = mPerFrameDrawRecordVectors[mRTVIndex];
			GraphicsVector& FrameGlobalsVector = mPerFrameFrameGlobalsVectors[mRTVIndex];

			size_t FrameGlobalsSizeInBytes = sizeof(Game::RFD::FrameGlobals);
			size_t ModelContextsSizeInBytes = sizeof(Game::RFD::ModelContext) * Data.modelContexts.size();
			size_t DrawRecordsGpuSizeInBytes = sizeof(DrawRecordGPU) * mDrawRecordsGPU.size();

			std::byte DummyByte{ 0 };
			void* FrameGlobalsSourceData = FrameGlobalsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(&Data.globals);
			void* ModelContextSourceData = ModelContextsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(Data.modelContexts.data());
			void* DrawRecordSourceData = DrawRecordsGpuSizeInBytes == 0 ? &DummyByte : static_cast<void*>(mDrawRecordsGPU.data());

			bool FrameGlobalsCopyResult = FrameGlobalsVector.Copy(*mGraphicsAllocator, FrameGlobalsSourceData, FrameGlobalsSizeInBytes);
			ErrorHandler::report(FrameGlobalsCopyResult == false, "DirectQueue", "Failed to copy frame globals data.", ErrorHandler::Level::Critical);

			bool ModelContextCopyResult = ModelContextVector.Copy(*mGraphicsAllocator, ModelContextSourceData, ModelContextsSizeInBytes);
			ErrorHandler::report(ModelContextCopyResult == false, "DirectQueue", "Failed to copy model context data.", ErrorHandler::Level::Critical);

			bool DrawRecordCopyResult = DrawRecordVector.Copy(*mGraphicsAllocator, DrawRecordSourceData, DrawRecordsGpuSizeInBytes);
			ErrorHandler::report(DrawRecordCopyResult == false, "DirectQueue", "Failed to copy draw record data.", ErrorHandler::Level::Critical);

			std::array<Interface::CopyQueueCopyRequest, 3> CopyRequests{ FrameGlobalsVector.CreateCopyQueueCopyRequest(*mGraphicsAllocator, 0), ModelContextVector.CreateCopyQueueCopyRequest(*mGraphicsAllocator, 0), DrawRecordVector.CreateCopyQueueCopyRequest(*mGraphicsAllocator, 0) };
			uint64_t FenceValue = mCopyQueue->EnqueueCopy(CopyRequests);
			mCopyQueue->DispatchCopies();
			mPerFrameCopyFenceValues[mRTVIndex] = FenceValue;

			DirectQueue::UpdateShaderResourceViews(mRTVIndex, 1, static_cast<uint32_t>(Data.modelContexts.size()), static_cast<uint32_t>(mDrawRecordsGPU.size()));
		}

		bool DirectQueue::CompareDrawRecordByPso(const Game::RFD::DrawRecord& Left, const Game::RFD::DrawRecord& Right) {
			if (Left.pass != Right.pass) {
				return Left.pass < Right.pass;
			}

			if (Left.pso != Right.pso) {
				return Left.pso < Right.pso;
			}

			if (Left.mesh != Right.mesh) {
				return Left.mesh < Right.mesh;
			}

			return Left.submesh < Right.submesh;
		}

		void DirectQueue::BuildDrawRecordGpu(const Game::RFD::RenderFrameData& Data) {
			mDrawRecordsGPU.clear();
			mDrawRecordsGPU.reserve(Data.drawRecords.size());

			for (const Game::RFD::DrawRecord& DrawRecord : Data.drawRecords) {
				DrawRecordGPU DrawRecordGpu{};
				DrawRecordGpu.ObjectIndex = DrawRecord.objectIndex;
				DrawRecordGpu.MaterialIndex = DrawRecord.materialIndex;
				DrawRecordGpu.Flags = DrawRecord.flags;
				DrawRecordGpu.Pad0 = DrawRecord.pad0;

				mDrawRecordsGPU.push_back(DrawRecordGpu);
			}
		}

		void DirectQueue::UpdateShaderResourceViews(uint32_t RtvIndex, uint32_t FrameGlobalsCount, uint32_t ModelContextCount, uint32_t DrawRecordCount) {
			GraphicsVector& FrameGlobalsVector = mPerFrameFrameGlobalsVectors[RtvIndex];
			GraphicsVector& ModelContextVector = mPerFrameModelContextVectors[RtvIndex];
			GraphicsVector& DrawRecordVector = mPerFrameDrawRecordVectors[RtvIndex];
			ID3D12Resource* FrameGlobalsResource = FrameGlobalsVector.GetResource();
			ID3D12Resource* ModelContextResource = ModelContextVector.GetResource();
			ID3D12Resource* DrawRecordResource = DrawRecordVector.GetResource();

			if (FrameGlobalsVector.IsValid() == true) {
				bool IsUpdateRequired = DirectQueue::IsShaderResourceViewUpdateRequired(mFrameGlobalsSrvResources[RtvIndex], FrameGlobalsResource, mFrameGlobalsSrvElementCounts[RtvIndex], FrameGlobalsCount);
				if (IsUpdateRequired == true) {
					FrameGlobalsVector.CreateShaderResourceView(mDevice.Get(), mFrameGlobalsSrvHandles[RtvIndex].GetCPU(), DXGI_FORMAT_UNKNOWN, 0, FrameGlobalsCount, sizeof(Game::RFD::FrameGlobals), D3D12_BUFFER_SRV_FLAG_NONE);
					mFrameGlobalsSrvResources[RtvIndex] = FrameGlobalsResource;
					mFrameGlobalsSrvElementCounts[RtvIndex] = FrameGlobalsCount;
				}
			}
			else {
				mFrameGlobalsSrvResources[RtvIndex] = nullptr;
				mFrameGlobalsSrvElementCounts[RtvIndex] = 0;
			}

			if (ModelContextVector.IsValid() == true) {
				bool IsUpdateRequired = DirectQueue::IsShaderResourceViewUpdateRequired(mModelContextSrvResources[RtvIndex], ModelContextResource, mModelContextSrvElementCounts[RtvIndex], ModelContextCount);
				if (IsUpdateRequired == true) {
					ModelContextVector.CreateShaderResourceView(mDevice.Get(), mModelContextSrvHandles[RtvIndex].GetCPU(), DXGI_FORMAT_UNKNOWN, 0, ModelContextCount, sizeof(Game::RFD::ModelContext), D3D12_BUFFER_SRV_FLAG_NONE);
					mModelContextSrvResources[RtvIndex] = ModelContextResource;
					mModelContextSrvElementCounts[RtvIndex] = ModelContextCount;
				}
			}
			else {
				mModelContextSrvResources[RtvIndex] = nullptr;
				mModelContextSrvElementCounts[RtvIndex] = 0;
			}

			if (DrawRecordVector.IsValid() == true) {
				bool IsUpdateRequired = DirectQueue::IsShaderResourceViewUpdateRequired(mDrawRecordSrvResources[RtvIndex], DrawRecordResource, mDrawRecordSrvElementCounts[RtvIndex], DrawRecordCount);
				if (IsUpdateRequired == true) {
					DrawRecordVector.CreateShaderResourceView(mDevice.Get(), mDrawRecordSrvHandles[RtvIndex].GetCPU(), DXGI_FORMAT_UNKNOWN, 0, DrawRecordCount, sizeof(DrawRecordGPU), D3D12_BUFFER_SRV_FLAG_NONE);
					mDrawRecordSrvResources[RtvIndex] = DrawRecordResource;
					mDrawRecordSrvElementCounts[RtvIndex] = DrawRecordCount;
				}
			}
			else {
				mDrawRecordSrvResources[RtvIndex] = nullptr;
				mDrawRecordSrvElementCounts[RtvIndex] = 0;
			}
		}

		bool DirectQueue::IsShaderResourceViewUpdateRequired(ID3D12Resource* CachedResource, ID3D12Resource* CurrentResource, uint32_t CachedElementCount, uint32_t CurrentElementCount) const {
			if (CurrentResource == nullptr) {
				return false;
			}

			if (CachedResource != CurrentResource) {
				return true;
			}

			if (CachedElementCount != CurrentElementCount) {
				return true;
			}

			return false;
		}


		// data 를 순회하며 draw call 을 commandlist 에 쌓는 함수. 루프 구성 방식은 아래를 참고한다.
		// Render Loop 참고 순서
		// 1) drawRecords를 pass, pso, mesh, submesh 키로 정렬한다.
		// 2) 정렬된 순서에 맞춰 GPU 드로우 레코드를 재구성하고 업로드한다.
		// 3) drawRecords를 앞에서부터 순회하며 동일 키 구간(run)을 찾는다.
		// 4) 각 run 시작 레코드에서 pso와 mesh/submesh를 꺼내 상태를 설정한다.
		// 5) DrawIndexedInstanced 호출 인자는 다음처럼 잡는다.
		//    - IndexCountPerInstance: run 시작 레코드의 mesh/submesh에서 얻은 index count
		//    - InstanceCount: run 길이
		//    - StartIndexLocation/BaseVertexLocation: run 시작 레코드의 mesh/submesh에서 얻은 값
		//    - StartInstanceLocation: 0
		// 6) run 시작 인덱스는 루트 셰이더 상수(예: DrawRecordBaseIndex)로 셰이더에 전달한다.
		// 7) 셰이더에서는 drawIndex = DrawRecordBaseIndex + SV_InstanceID로 GPU 드로우 레코드를 조회한다.
		// 8) 조회한 record.objectIndex로 modelContexts를, record.materialIndex로 머티리얼 테이블을 인덱싱한다.

		void DirectQueue::DrawForward(Game::RFD::RenderFrameData& data) {
			const Interface::IPipeline* ActivePipeline = nullptr;

			size_t DrawRecordIndex = 0;
			while (DrawRecordIndex < data.drawRecords.size()) {
				Game::RFD::DrawRecord& StartRecord = data.drawRecords[DrawRecordIndex];

				if (StartRecord.pso == nullptr || StartRecord.mesh == nullptr) {
					DrawRecordIndex += 1;
					continue;
				}

				size_t RunEndIndex = DrawRecordIndex + 1;
				while (RunEndIndex < data.drawRecords.size()) {
					const Game::RFD::DrawRecord& NextRecord = data.drawRecords[RunEndIndex];
					bool IsSameRun = NextRecord.pass == StartRecord.pass && NextRecord.pso == StartRecord.pso && NextRecord.mesh == StartRecord.mesh && NextRecord.submesh == StartRecord.submesh;
					if (IsSameRun == false) {
						break;
					}

					RunEndIndex += 1;
				}

				ActivePipeline = StartRecord.pso->Set(ActivePipeline, mCommandList.Get());

				DrawRootConstantsB1 RootConstants{};
				RootConstants.FrameGlobalsSrvIndex = mFrameGlobalsSrvHandles[mRTVIndex].GetIndex();
				RootConstants.ModelContextSrvIndex = mModelContextSrvHandles[mRTVIndex].GetIndex();
				RootConstants.DrawRecordSrvIndex = mDrawRecordSrvHandles[mRTVIndex].GetIndex();
				RootConstants.DrawRecordBaseIndex = static_cast<uint32_t>(DrawRecordIndex);
				mCommandList->SetGraphicsRoot32BitConstants(0, sizeof(DrawRootConstantsB1) / sizeof(uint32_t), &RootConstants, 0);

				mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				const std::vector<D3D12_VERTEX_BUFFER_VIEW>& VertexBufferViews = StartRecord.mesh->GetVertexBufferViews();
				if (VertexBufferViews.empty() == false) {
					mCommandList->IASetVertexBuffers(0, static_cast<UINT>(VertexBufferViews.size()), VertexBufferViews.data());
				}

				const D3D12_INDEX_BUFFER_VIEW& IndexBufferView = StartRecord.mesh->GetIndexBufferView();
				mCommandList->IASetIndexBuffer(&IndexBufferView);

				const Game::ModelSubMesh& SubMesh = StartRecord.mesh->GetSubMesh(StartRecord.submesh);
				UINT IndexCountPerInstance = static_cast<UINT>(SubMesh.IndexCount);
				UINT InstanceCount = static_cast<UINT>(RunEndIndex - DrawRecordIndex);
				UINT StartIndexLocation = static_cast<UINT>(SubMesh.IndexOffset);
				INT BaseVertexLocation = 0;
				UINT StartInstanceLocation = 0;

				mCommandList->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
				DrawRecordIndex = RunEndIndex;
			}

		}

		void DirectQueue::Render(Game::RFD::RenderFrameData& data) {
			ErrorHandler::report(mCopyQueue == nullptr, "DirectQueue", "CopyQueue is not set.", ErrorHandler::Level::Critical);

			auto currentIndex = mFrameSync.GetCurrentIndex();

			auto& allocator = mMainCommandAllocators[currentIndex];
			allocator->Reset();
			mCommandList->Reset(allocator.Get(), nullptr);

			std::array<ID3D12DescriptorHeap*, 1> DescriptorHeaps{ mSrvHeap.GetHeap() };
			mCommandList->SetDescriptorHeaps(static_cast<UINT>(DescriptorHeaps.size()), DescriptorHeaps.data());

			auto& rt = mRenderTargets[currentIndex];
			rt->Transition(mCommandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

			GraphicsVector& ModelContextVector = mPerFrameModelContextVectors[currentIndex];
			GraphicsVector& DrawRecordVector = mPerFrameDrawRecordVectors[currentIndex];
			GraphicsVector& FrameGlobalsVector = mPerFrameFrameGlobalsVectors[currentIndex];

			if (FrameGlobalsVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER FrameGlobalsBarrier = FrameGlobalsVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
				mCommandList->ResourceBarrier(1, &FrameGlobalsBarrier);
			}

			if (ModelContextVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER ModelContextBarrier = ModelContextVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
				mCommandList->ResourceBarrier(1, &ModelContextBarrier);
			}

			if (DrawRecordVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER DrawRecordBarrier = DrawRecordVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
				mCommandList->ResourceBarrier(1, &DrawRecordBarrier);
			}

			auto rtv = rt->GetRTV();
			auto dsv = mDepthStencilBuffer->GetDSV();

			mCommandList->ClearRenderTargetView(rtv, DirectX::Colors::Blue, 0, nullptr);


			mCommandList->ClearDepthStencilView(mDepthStencilBuffer->GetDSV(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
			mCommandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

			mCommandList->RSSetViewports(1, &mViewport);
			mCommandList->RSSetScissorRects(1, &mScissorRect);


			// Execute Render Tasks
			DirectQueue::DrawForward(data);
			mWidgetCore.Render(mCommandList);



			if (ModelContextVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER ModelContextBarrier = ModelContextVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST);
				mCommandList->ResourceBarrier(1, &ModelContextBarrier);
			}

			if (DrawRecordVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER DrawRecordBarrier = DrawRecordVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST);
				mCommandList->ResourceBarrier(1, &DrawRecordBarrier);
			}

			if (FrameGlobalsVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER FrameGlobalsBarrier = FrameGlobalsVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST);
				mCommandList->ResourceBarrier(1, &FrameGlobalsBarrier);
			}




			rt->Transition(mCommandList.Get(), D3D12_RESOURCE_STATE_PRESENT);



			mCommandList->Close();
			uint64_t CopyFenceValue = mPerFrameCopyFenceValues[currentIndex];
			if (CopyFenceValue != 0) {
				mCopyQueue->WaitForFence(CopyFenceValue);
			}


			ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
			mDirectCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

			ErrorHandler::report(mSwapChain->Present(Constants::AllowTearing ? 0 : 1, Constants::AllowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0), "DirectQueue", "Failed to present SwapChain.", ErrorHandler::Level::Critical);

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
				ErrorHandler::report("DirectQueue", "No suitable GPU found.", ErrorHandler::Level::Critical);
			}

			auto hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(mDevice.GetAddressOf()));

			if (FAILED(hr)) {
				ComPtr<IDXGIAdapter> warpAdapter{ nullptr };
				ErrorHandler::report(mFactory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)),"DirectQueue", "Falied to make WarpAdapter", ErrorHandler::Level::Critical);
				ErrorHandler::report(::D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&mDevice)), "DirectQueue", "Failed to make Warp Device", ErrorHandler::Level::Critical);
			}

			mWidgetCore.Initialize(mHwnd, mDevice);

			StdOutput::PrintLine("Console Test");

#if defined(DEBUG) || defined(_DEBUG)
			mDevice->QueryInterface(IID_PPV_ARGS(mD3D12InfoQueue.GetAddressOf()));
			if (mD3D12InfoQueue != nullptr) {
				mD3D12InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
				mD3D12InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
			}
#endif

			if (DirectQueue::CheckShaderModelSupport(D3D_SHADER_MODEL_6_6)) {
				StdOutput::PrintLine("Shader Model 6.6 is supported.");
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

			for (uint64_t& FenceValue : mPerFrameCopyFenceValues) {
				FenceValue = 0;
			}
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
			mSrvHeap = DescriptorHeap(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Constants::FrameCount<uint32_t> * 3, true);

			for (auto&& [i, rt] : views::enumerate(mRenderTargets)) {
				ComPtr<ID3D12Resource> backBuffer{ nullptr };
				ErrorHandler::report(mSwapChain->GetBuffer(static_cast<UINT>(i), IID_PPV_ARGS(backBuffer.GetAddressOf())), "DirectQueue", "Failed to get SwapChain BackBuffer.", ErrorHandler::Level::Critical);
				rt = Texture::CreateFromResource(backBuffer.Get(), "BackBuffer_" + std::to_string(i));

				rt->CreateRTV(mDevice.Get(), mRTVHeap);
			}

			for (size_t Index = 0; Index < Constants::FrameCount<size_t>; ++Index) {
				mFrameGlobalsSrvHandles[Index] = mSrvHeap.Allocate();
				mModelContextSrvHandles[Index] = mSrvHeap.Allocate();
				mDrawRecordSrvHandles[Index] = mSrvHeap.Allocate();
			}



			mDSVHeap = DescriptorHeap(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

			CD3DX12_CLEAR_VALUE depthOptimizedClearValue{ DXGI_FORMAT_D24_UNORM_S8_UINT, 1.0f, 0 };
			mDepthStencilBuffer = Texture::CreateTarget(mDevice.Get(), Config::Query().Get<uint32_t>("Window_Width"), Config::Query().Get<uint32_t>("Window_Height"), DXGI_FORMAT_D24_UNORM_S8_UINT, TextureUsage::DepthStencil, &depthOptimizedClearValue);

			mDepthStencilBuffer->CreateDSV(mDevice.Get(), mDSVHeap);
		}

		ComPtr<IDXGIAdapter1> DirectQueue::GetBestAdapter() {
			StdOutput::PrintLine("\n\n====================Selecting Adapter====================\n");

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

				
				StdOutput::Print("Adapter{:^3} : {} | VRAM: {} MB\n", i, ConvertWstringToUtf8(desc.Description), desc.DedicatedVideoMemory / (1024 * 1024));

				if (desc.DedicatedVideoMemory > maxVRAM) {
					maxVRAM = desc.DedicatedVideoMemory;
					bestAdapter = adapter;
				}
			}

			if (bestAdapter) {
				DXGI_ADAPTER_DESC1 bestDesc;
				bestAdapter->GetDesc1(&bestDesc);
				StdOutput::Print("Selected Adapter: {} | VRAM: {} MB\n", ConvertWstringToUtf8(bestDesc.Description), bestDesc.DedicatedVideoMemory / (1024 * 1024));
			}
			else {
				StdOutput::PrintLine("No suitable GPU found.");
			}
			StdOutput::PrintLine("\n=========================================================\n");

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

					StdOutput::PrintLine("DXGI: {}", message->pDescription);
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

					StdOutput::PrintLine("D3D12: {}", message->pDescription);
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
