#pragma once 
#include <array>
#include "Core/DX/GraphicsBuffer.h"
#include "Core/DX/FrameSync.h"
#include "Core/RenderWorker.h"
#include "Utility/DirectXInclude.h"
#include "Utility/CompileTimeConstants.h"

namespace Core {
	namespace DX {
		class DirectQueue {
		public:
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
			std::array<ComPtr<ID3D12CommandAllocator>, Constants::FrameCount<size_t>> mCommandAllocators{};

			ComPtr<ID3D12DescriptorHeap> mRTVHeap{ nullptr };
			std::array<GraphicsResource, Constants::FrameCount<size_t>> mRenderTargets{};
			uint32_t mRTVIndex{}; 

			ComPtr<ID3D12DescriptorHeap> mDSVHeap{ nullptr };
			GraphicsResource mDepthStencilBuffer{};

			Core::Task::RenderFlowContext mRenderContext;
		};

	}
}