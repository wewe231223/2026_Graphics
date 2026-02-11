#pragma once 
#include <array>
#include "Core/DX/DesciptorHeap.h"
#include "Core/DX/GraphicsBuffer.h"
#include "Core/DX/FrameSync.h"
#include "Core/DX/Texture.h"
#include "Core/Config.h"
#include "Utility/DirectXInclude.h"
#include "Utility/CompileTimeConstants.h"
#include "Utility/FixedArray.h"

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
			ID3D12Device* GetDevice() const;

			void Update(); 
		private:
			void InitBasements(); 
			void InitWorkers();
			void InitCommandList();
			void InitTargetResources(); 

			ComPtr<IDXGIAdapter1> GetBestAdapter(); 

			bool CheckShaderModelSupport(D3D_SHADER_MODEL);
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

			D3D12_VIEWPORT mViewport{ 0, 0, Config::Query().Get<float>("Window_Width"), Config::Query().Get<float>("Window_Height"), 0.f, 1.f };
			D3D12_RECT mScissorRect{ 0, 0, Config::Query().Get<LONG>("Window_Width"), Config::Query().Get<LONG>("Window_Height") };
		};

	}
}
