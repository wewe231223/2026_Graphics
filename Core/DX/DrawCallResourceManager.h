#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "Core/Common.h"
#include "Core/DX/DesciptorHeap.h"
#include "Core/DX/GraphicsVector.h"
#include "Game/Base/RenderFrameData.h"
#include "Utility/CompileTimeConstants.h"

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
			void Initialize(ID3D12Device* Device, DescriptorHeap* SrvHeap);
			void PrepareFrameResources(uint32_t RtvIndex, Game::RFD::RenderFrameData& Data, GraphicsAllocator& GraphicsAllocator, Interface::ICopyQueue& CopyQueue);
			void TransitionToShaderResource(ID3D12GraphicsCommandList* CommandList, uint32_t RtvIndex);
			void TransitionToCopyDestination(ID3D12GraphicsCommandList* CommandList, uint32_t RtvIndex);
			void WaitForUpload(Interface::ICopyQueue& CopyQueue, uint32_t RtvIndex) const;

			DescriptorHandle GetFrameGlobalsSrvHandle(uint32_t RtvIndex) const;
			DescriptorHandle GetModelContextSrvHandle(uint32_t RtvIndex) const;
			DescriptorHandle GetDrawRecordSrvHandle(uint32_t RtvIndex) const;
			DescriptorHandle GetMaterialSrvHandle(uint32_t RtvIndex) const;
			DescriptorHandle GetMaterialTextureTableSrvHandle(uint32_t RtvIndex) const;

		private:
			static bool CompareDrawRecordByPso(const Game::RFD::DrawRecord& Left, const Game::RFD::DrawRecord& Right);
			void BuildDrawRecordGpu(const Game::RFD::RenderFrameData& Data);
			void UpdateShaderResourceViews(uint32_t RtvIndex, uint32_t FrameGlobalsCount, uint32_t ModelContextCount, uint32_t DrawRecordCount, uint32_t MaterialCount, uint32_t MaterialTextureTableCount);
			bool IsShaderResourceViewUpdateRequired(ID3D12Resource* CachedResource, ID3D12Resource* CurrentResource, uint32_t CachedElementCount, uint32_t CurrentElementCount) const;

		private:
			ID3D12Device* mDevice{};
			DescriptorHeap* mSrvHeap{};

			std::array<DescriptorHandle, Constants::FrameCount<size_t>> mFrameGlobalsSrvHandles{};
			std::array<DescriptorHandle, Constants::FrameCount<size_t>> mModelContextSrvHandles{};
			std::array<DescriptorHandle, Constants::FrameCount<size_t>> mDrawRecordSrvHandles{};
			std::array<DescriptorHandle, Constants::FrameCount<size_t>> mMaterialSrvHandles{};
			std::array<DescriptorHandle, Constants::FrameCount<size_t>> mMaterialTextureTableSrvHandles{};

			std::array<ID3D12Resource*, Constants::FrameCount<size_t>> mFrameGlobalsSrvResources{};
			std::array<ID3D12Resource*, Constants::FrameCount<size_t>> mModelContextSrvResources{};
			std::array<ID3D12Resource*, Constants::FrameCount<size_t>> mDrawRecordSrvResources{};
			std::array<ID3D12Resource*, Constants::FrameCount<size_t>> mMaterialSrvResources{};
			std::array<ID3D12Resource*, Constants::FrameCount<size_t>> mMaterialTextureTableSrvResources{};

			std::array<uint32_t, Constants::FrameCount<size_t>> mFrameGlobalsSrvElementCounts{};
			std::array<uint32_t, Constants::FrameCount<size_t>> mModelContextSrvElementCounts{};
			std::array<uint32_t, Constants::FrameCount<size_t>> mDrawRecordSrvElementCounts{};
			std::array<uint32_t, Constants::FrameCount<size_t>> mMaterialSrvElementCounts{};
			std::array<uint32_t, Constants::FrameCount<size_t>> mMaterialTextureTableSrvElementCounts{};

			std::array<GraphicsVector, Constants::FrameCount<size_t>> mPerFrameFrameGlobalsVectors{};
			std::array<GraphicsVector, Constants::FrameCount<size_t>> mPerFrameModelContextVectors{};
			std::array<GraphicsVector, Constants::FrameCount<size_t>> mPerFrameDrawRecordVectors{};
			std::array<GraphicsVector, Constants::FrameCount<size_t>> mPerFrameMaterialVectors{};
			std::array<GraphicsVector, Constants::FrameCount<size_t>> mPerFrameMaterialTextureTableVectors{};

			std::array<uint64_t, Constants::FrameCount<size_t>> mPerFrameCopyFenceValues{};
			std::vector<DrawRecordGPU> mDrawRecordsGpu{};
		};
	}
}
