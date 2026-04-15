#pragma once
#include <array>
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

		class DirectQueue {
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

			DescriptorHeap* GetSrvHeap();

			void SetUploadInfrastructure(GraphicsAllocator* GraphicsAllocator, Interface::ICopyQueue* CopyQueue);
			void PreRender(Game::RFD::RenderFrameData& Data, float Dt);
			void Render(Game::RFD::RenderFrameData& Data, Widget::WidgetCore* WidgetCore);

		private:
			void InitBasements();
			void InitWorkers();
			void InitCommandList();
			void InitTargetResources();
			void EnsureShadowMapResources(const Game::RFD::ShadowMappingParameter& ShadowMappingParameter);

			ComPtr<IDXGIAdapter1> GetBestAdapter();

			bool CheckShaderModelSupport(D3D_SHADER_MODEL);

			void DrainDebugMessages();

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
			ComPtr<IDXGIAdapter1> mPrimaryAdapter{ nullptr };
			ComPtr<ID3D12CommandQueue> mDirectCommandQueue{ nullptr };

			ComPtr<IDXGISwapChain1> mSwapChain{ nullptr };

			ComPtr<ID3D12GraphicsCommandList> mCommandList{ nullptr };
			std::array<ComPtr<ID3D12CommandAllocator>, Constants::FrameCount<size_t>> mMainCommandAllocators{};

			DescriptorHeap mRTVHeap{};
			std::array<TexPtr, Constants::FrameCount<size_t>> mRenderTargets{};
			uint32_t mRTVIndex{};

			DescriptorHeap mDSVHeap{};
			TexPtr mDepthStencilBuffer{};
			DescriptorHeap mShadowDSVHeap{};
			TexPtr mShadowDepthMap{};
			uint32_t mShadowMapSize{};
			DescriptorHeap mSrvHeap{};
			std::array<DrawCallResourceManager, Constants::FrameCount<size_t>> mDrawCallResourceManagers{};
			MaterialResourceManager mMaterialResourceManager{};
			DrawCallDispatcher mDrawCallDispatcher{};


			FrameSync mFrameSync{};
			GraphicsAllocator* mGraphicsAllocator{ nullptr };
			Interface::ICopyQueue* mCopyQueue{ nullptr };


			D3D12_VIEWPORT mViewport{ 0, 0, Config::Query()->Get<float>("Window_Width"), Config::Query()->Get<float>("Window_Height"), 0.f, 1.f };
			D3D12_RECT mScissorRect{ 0, 0, Config::Query()->Get<LONG>("Window_Width"), Config::Query()->Get<LONG>("Window_Height") };
			D3D12_VIEWPORT mShadowViewport{};
			D3D12_RECT mShadowScissorRect{};
		};

	}
}
