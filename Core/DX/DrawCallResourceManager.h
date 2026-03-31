#pragma once
#include <cstdint>
#include <vector>
#include "Core/Common.h"
#include "Core/DX/DesciptorHeap.h"
#include "Core/DX/GraphicsVector.h"
#include "Game/Base/RenderFrameData.h"

namespace Core {
	namespace DX {
		class GraphicsAllocator;

		class DrawCallResourceManager {
		public:
			DrawCallResourceManager();
			~DrawCallResourceManager();
			DrawCallResourceManager(const DrawCallResourceManager& Other) = delete;
			DrawCallResourceManager& operator=(const DrawCallResourceManager& Other) = delete;
			DrawCallResourceManager(DrawCallResourceManager&& Other) = delete;
			DrawCallResourceManager& operator=(DrawCallResourceManager&& Other) = delete;

		public:
			void Initialize(ID3D12Device* Device, DescriptorHeap* SrvHeap, std::uint32_t FrameIndex);
			void PrepareFrameResources(Game::RFD::RenderFrameData& Data, GraphicsAllocator& GraphicsAllocator, Interface::ICopyQueue* CopyQueue);
			void TransitionToShaderResource(ID3D12GraphicsCommandList* CommandList);
			void TransitionToCopyDestination(ID3D12GraphicsCommandList* CommandList);
			void WaitForUpload(Interface::ICopyQueue* CopyQueue) const;

			DescriptorHandle GetFrameGlobalsSrvHandle() const;
			DescriptorHandle GetModelContextSrvHandle() const;
			DescriptorHandle GetBonePaletteSrvHandle() const;
			DescriptorHandle GetDrawRecordSrvHandle() const;

		private:
			static bool CompareDrawRecordByPso(const Game::RFD::DrawRecord& Left, const Game::RFD::DrawRecord& Right);
			void BuildDrawRecordGpu(const Game::RFD::RenderFrameData& Data);
			void UpdateShaderResourceViews(std::uint32_t FrameGlobalsCount, std::uint32_t ModelContextCount, std::uint32_t BonePaletteCount, std::uint32_t DrawRecordCount);
			bool IsShaderResourceViewUpdateRequired(ID3D12Resource* CachedResource, ID3D12Resource* CurrentResource, std::uint32_t CachedElementCount, std::uint32_t CurrentElementCount) const;

		private:
			ID3D12Device* mDevice{};
			DescriptorHeap* mSrvHeap{};

			DescriptorHandle mFrameGlobalsSrvHandle{};
			DescriptorHandle mModelContextSrvHandle{};
			DescriptorHandle mBonePaletteSrvHandle{};
			DescriptorHandle mDrawRecordSrvHandle{};

			ID3D12Resource* mFrameGlobalsSrvResource{};
			ID3D12Resource* mModelContextSrvResource{};
			ID3D12Resource* mBonePaletteSrvResource{};
			ID3D12Resource* mDrawRecordSrvResource{};

			std::uint32_t mFrameGlobalsSrvElementCount{};
			std::uint32_t mModelContextSrvElementCount{};
			std::uint32_t mBonePaletteSrvElementCount{};
			std::uint32_t mDrawRecordSrvElementCount{};

			GraphicsVector mFrameGlobalsVector{};
			GraphicsVector mModelContextVector{};
			GraphicsVector mBonePaletteVector{};
			GraphicsVector mDrawRecordVector{};

			Interface::CopyFuture mCopyFuture{};
			std::vector<DrawRecordGPU> mDrawRecordsGpu{};
		};
	}
}
