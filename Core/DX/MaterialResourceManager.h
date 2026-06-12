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

		class MaterialResourceManager {
		public:
			MaterialResourceManager();
			~MaterialResourceManager();
			MaterialResourceManager(const MaterialResourceManager& Other) = delete;
			MaterialResourceManager& operator=(const MaterialResourceManager& Other) = delete;
			MaterialResourceManager(MaterialResourceManager&& Other) = delete;
			MaterialResourceManager& operator=(MaterialResourceManager&& Other) = delete;

		public:
			void Initialize(ID3D12Device* Device, DescriptorHeap* SrvHeap);
			void PrepareFrameResources(std::uint32_t RtvIndex, const Game::RFD::RenderFrameData& Data, GraphicsAllocator& GraphicsAllocator, Interface::ICopyQueue* CopyQueue);
			void TransitionToShaderResource(ID3D12GraphicsCommandList* CommandList, std::uint32_t RtvIndex);
			void TransitionToCopyDestination(ID3D12GraphicsCommandList* CommandList, std::uint32_t RtvIndex);
			void QueueWaitForUpload(ID3D12CommandQueue* WaitingQueue, std::uint32_t RtvIndex) const;

			DescriptorHandle GetMaterialSrvHandle() const;
			DescriptorHandle GetMaterialTextureTableSrvHandle(std::uint32_t RtvIndex) const;

		private:
			static std::uint64_t ComputeDataHash(const void* Data, std::size_t SizeInBytes);
			void UpdateMaterialShaderResourceView(std::uint32_t MaterialCount);
			void UpdateMaterialTextureTableShaderResourceView(std::uint32_t RtvIndex, std::uint32_t MaterialTextureTableCount);
			bool IsShaderResourceViewUpdateRequired(ID3D12Resource* CachedResource, ID3D12Resource* CurrentResource, std::uint32_t CachedElementCount, std::uint32_t CurrentElementCount) const;

		private:
			ID3D12Device* mDevice{};
			DescriptorHeap* mSrvHeap{};

			DescriptorHandle mMaterialSrvHandle{};
			std::array<DescriptorHandle, Constants::FrameCount<std::size_t>> mMaterialTextureTableSrvHandles{};

			ID3D12Resource* mMaterialSrvResource{};
			std::array<ID3D12Resource*, Constants::FrameCount<std::size_t>> mMaterialTextureTableSrvResources{};

			std::uint32_t mMaterialSrvElementCount{};
			std::array<std::uint32_t, Constants::FrameCount<std::size_t>> mMaterialTextureTableSrvElementCounts{};

			GraphicsVector mMaterialVector{};
			std::array<GraphicsVector, Constants::FrameCount<std::size_t>> mPerFrameMaterialTextureTableVectors{};

			std::uint64_t mMaterialHash{};
			std::size_t mMaterialSizeInBytes{};
			std::array<std::uint64_t, Constants::FrameCount<std::size_t>> mPerFrameMaterialTextureTableHashes{};
			std::array<std::size_t, Constants::FrameCount<std::size_t>> mPerFrameMaterialTextureTableSizesInBytes{};

			std::array<Interface::Future, Constants::FrameCount<std::size_t>> mPerFrameCopyFutures{};
		};
	}
}
