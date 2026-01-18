#pragma once 
#include <array>
#include "Core/DX/DesciptorHeap.h"
#include "Core/DX/GraphicsBuffer.h"
#include "Core/DX/FrameSync.h"
#include "Core/RenderWorker.h"
#include "Core/DX/Texture.h"
#include "Utility/DirectXInclude.h"
#include "Utility/CompileTimeConstants.h"
#include "Utility/FixedArray.h"

// TODO 
// Direct Queue 구현하기

namespace Core {
	namespace DX {
		class DirectQueue {
		public:
			DirectQueue(HWND hWnd);
			~DirectQueue(); 

			DirectQueue(const DirectQueue& other) = delete;
			DirectQueue& operator=(const DirectQueue& other) = delete;

			DirectQueue(DirectQueue&& other) = delete;
			DirectQueue& operator=(DirectQueue&& other) = delete;

		public:
			void Update(); 
		private:
			void InitBasements(); 
			void InitWorkers();
			void InitCommandList();
			void InitTargetResources(); 

			ComPtr<IDXGIAdapter1> GetBestAdapter(); 

		private:
			HWND mHwnd{ nullptr };
			ComPtr<IDXGIFactory6> mFactory{ nullptr };

		#if defined(DEBUG) || defined(_DEBUG)
			ComPtr<ID3D12Debug6> mDebugController{ nullptr };
			ComPtr<IDXGIDebug1> mDebugDXGI{ nullptr };
		#endif 

			ComPtr<ID3D12Device> mDevice{ nullptr };
			ComPtr<ID3D12CommandQueue> mDirectCommandQueue{ nullptr };

			ComPtr<IDXGISwapChain1> mSwapChain{ nullptr }; 

			ComPtr<ID3D12GraphicsCommandList> mCommandList{ nullptr };
			std::array<ComPtr<ID3D12CommandAllocator>, Constants::FrameCount<size_t>> mMainCommandAllocators{};

			DescriptorHeap mRTVHeap{};
			std::array<TexPtr, Constants::FrameCount<size_t>> mRenderTargets{};
			uint32_t mRTVIndex{}; 

			DescriptorHeap mDSVHeap{};
			TexPtr mDepthStencilBuffer{};

			// 메인 쓰레드, Compute Queue 쓰레드 제외
			FrameSync mFrameSync{};
			Core::Task::RenderFlowContext mRenderContext{ static_cast<int>(std::thread::hardware_concurrency() - 2) };

			Cont::FixedArray<Core::Task::RenderWorker> mRenderWorkers{};
		};

	}
}