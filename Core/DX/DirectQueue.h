#pragma once 
#include <array>
#include <vector>
#include "Core/Common.h"
#include "Core/DX/DesciptorHeap.h"
#include "Core/DX/GraphicsVector.h"
#include "Core/DX/FrameSync.h"
#include "Core/DX/Texture.h"
#include "Core/Config.h"
#include "Utility/DirectXInclude.h"
#include "Utility/CompileTimeConstants.h"
#include "Utility/FixedArray.h"
#include "Game/Base/RenderFrameData.h"


namespace Core {
	namespace DX {
		class GraphicsAllocator;

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

			void SetUploadInfrastructure(GraphicsAllocator* GraphicsAllocator, Interface::ICopyQueue* CopyQueue);
			void PreRender(Game::RFD::RenderFrameData& data, float Dt);
			void Render(Game::RFD::RenderFrameData& data);

		private:
			void InitBasements(); 
			void InitWorkers();
			void InitCommandList();
			void InitTargetResources(); 

			ComPtr<IDXGIAdapter1> GetBestAdapter(); 

			bool CheckShaderModelSupport(D3D_SHADER_MODEL);

			void DrainDebugMessages();
			void UpdateShaderResourceViews(uint32_t RtvIndex, uint32_t ModelContextCount, uint32_t DrawRecordCount);
			bool IsShaderResourceViewUpdateRequired(ID3D12Resource* CachedResource, ID3D12Resource* CurrentResource, uint32_t CachedElementCount, uint32_t CurrentElementCount) const;

		private:
			static bool CompareDrawRecordByPso(const Game::RFD::DrawRecord& Left, const Game::RFD::DrawRecord& Right);
			void BuildDrawRecordGpu(const Game::RFD::RenderFrameData& Data);

			void DrawForward(Game::RFD::RenderFrameData& data); 

		private:
			HWND mHwnd{ nullptr };
			ComPtr<IDXGIFactory6> mFactory{ nullptr };
			 
		#if defined(DEBUG) || defined(_DEBUG)
			ComPtr<ID3D12Debug6> mDebugController{ nullptr };
			ComPtr<IDXGIDebug1> mDebugDXGI{ nullptr };
			ComPtr<IDXGIInfoQueue> mDxgiInfoQueue{ nullptr };
			ComPtr<ID3D12InfoQueue> mD3D12InfoQueue{ nullptr };
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

			DescriptorHeap mSrvHeap{};
			std::array<DescriptorHandle, Constants::FrameCount<size_t>> mModelContextSrvHandles{};
			std::array<DescriptorHandle, Constants::FrameCount<size_t>> mDrawRecordSrvHandles{};
			std::array<ID3D12Resource*, Constants::FrameCount<size_t>> mModelContextSrvResources{};
			std::array<ID3D12Resource*, Constants::FrameCount<size_t>> mDrawRecordSrvResources{};
			std::array<uint32_t, Constants::FrameCount<size_t>> mModelContextSrvElementCounts{};
			std::array<uint32_t, Constants::FrameCount<size_t>> mDrawRecordSrvElementCounts{};

			FrameSync mFrameSync{};
			GraphicsAllocator* mGraphicsAllocator{ nullptr };
			Interface::ICopyQueue* mCopyQueue{ nullptr };

			std::array<GraphicsVector, Constants::FrameCount<size_t>> mPerFrameModelContextVectors{};
			std::array<GraphicsVector, Constants::FrameCount<size_t>> mPerFrameDrawRecordVectors{};
			std::array<uint64_t, Constants::FrameCount<size_t>> mPerFrameCopyFenceValues{};
			std::vector<DrawRecordGPU> mDrawRecordsGPU{};

			D3D12_VIEWPORT mViewport{ 0, 0, Config::Query().Get<float>("Window_Width"), Config::Query().Get<float>("Window_Height"), 0.f, 1.f };
			D3D12_RECT mScissorRect{ 0, 0, Config::Query().Get<LONG>("Window_Width"), Config::Query().Get<LONG>("Window_Height") };
		};

	}
}
