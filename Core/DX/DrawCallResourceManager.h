#pragma once
#include <array>
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
			void QueueWaitForUpload(ID3D12CommandQueue* WaitingQueue) const;

			DescriptorHandle GetFrameGlobalsSrvHandle() const;
			DescriptorHandle GetShadowFrameGlobalsSrvHandle() const;
			DescriptorHandle GetShadowMappingParameterSrvHandle() const;
			DescriptorHandle GetModelContextSrvHandle() const;
			DescriptorHandle GetBoundingBoxContextSrvHandle() const;
			DescriptorHandle GetDebugGeometryContextSrvHandle() const;
			DescriptorHandle GetTerrainPatchContextSrvHandle() const;
			DescriptorHandle GetBonePaletteSrvHandle() const;
			DescriptorHandle GetDrawRecordSrvHandle() const;
			DescriptorHandle GetShadowModelContextSrvHandle(std::uint32_t ShadowCascadeIndex) const;
			DescriptorHandle GetShadowTerrainPatchContextSrvHandle(std::uint32_t ShadowCascadeIndex) const;
			DescriptorHandle GetShadowDrawRecordSrvHandle(std::uint32_t ShadowCascadeIndex) const;

		private:
			static bool CompareDrawRecordByPso(const Game::RFD::DrawRecord& Left, const Game::RFD::DrawRecord& Right);
			static bool IsDrawRecordsSorted(const std::vector<Game::RFD::DrawRecord>& DrawRecords);
			static void SortShadowDrawRecords(std::vector<Game::RFD::DrawRecord>& DrawRecords);
			void BuildDrawRecordGpu(const std::vector<Game::RFD::DrawRecord>& DrawRecords, std::vector<DrawRecordGPU>& OutDrawRecordsGpu);
			void UpdateShaderResourceViews(std::uint32_t FrameGlobalsCount, std::uint32_t ShadowFrameGlobalsCount, std::uint32_t ShadowMappingParameterCount, std::uint32_t ModelContextCount, std::uint32_t BoundingBoxContextCount, std::uint32_t DebugGeometryContextCount, std::uint32_t TerrainPatchContextCount, std::uint32_t BonePaletteCount, std::uint32_t DrawRecordCount);
			void UpdateShadowShaderResourceViews(std::uint32_t ShadowCascadeCount);
			bool IsShaderResourceViewUpdateRequired(ID3D12Resource* CachedResource, ID3D12Resource* CurrentResource, std::uint32_t CachedElementCount, std::uint32_t CurrentElementCount) const;

		private:
			ID3D12Device* mDevice{};
			DescriptorHeap* mSrvHeap{};

			DescriptorHandle mFrameGlobalsSrvHandle{};
			DescriptorHandle mShadowFrameGlobalsSrvHandle{};
			DescriptorHandle mShadowMappingParameterSrvHandle{};
			DescriptorHandle mModelContextSrvHandle{};
			DescriptorHandle mBoundingBoxContextSrvHandle{};
			DescriptorHandle mDebugGeometryContextSrvHandle{};
			DescriptorHandle mTerrainPatchContextSrvHandle{};
			DescriptorHandle mBonePaletteSrvHandle{};
			DescriptorHandle mDrawRecordSrvHandle{};
			std::array<DescriptorHandle, Game::RFD::ShadowCascadeMaxCount> mShadowModelContextSrvHandles{};
			std::array<DescriptorHandle, Game::RFD::ShadowCascadeMaxCount> mShadowTerrainPatchContextSrvHandles{};
			std::array<DescriptorHandle, Game::RFD::ShadowCascadeMaxCount> mShadowDrawRecordSrvHandles{};

			ID3D12Resource* mFrameGlobalsSrvResource{};
			ID3D12Resource* mShadowFrameGlobalsSrvResource{};
			ID3D12Resource* mShadowMappingParameterSrvResource{};
			ID3D12Resource* mModelContextSrvResource{};
			ID3D12Resource* mBoundingBoxContextSrvResource{};
			ID3D12Resource* mDebugGeometryContextSrvResource{};
			ID3D12Resource* mTerrainPatchContextSrvResource{};
			ID3D12Resource* mBonePaletteSrvResource{};
			ID3D12Resource* mDrawRecordSrvResource{};
			std::array<ID3D12Resource*, Game::RFD::ShadowCascadeMaxCount> mShadowModelContextSrvResources{};
			std::array<ID3D12Resource*, Game::RFD::ShadowCascadeMaxCount> mShadowTerrainPatchContextSrvResources{};
			std::array<ID3D12Resource*, Game::RFD::ShadowCascadeMaxCount> mShadowDrawRecordSrvResources{};

			std::uint32_t mFrameGlobalsSrvElementCount{};
			std::uint32_t mShadowFrameGlobalsSrvElementCount{};
			std::uint32_t mShadowMappingParameterSrvElementCount{};
			std::uint32_t mModelContextSrvElementCount{};
			std::uint32_t mBoundingBoxContextSrvElementCount{};
			std::uint32_t mDebugGeometryContextSrvElementCount{};
			std::uint32_t mTerrainPatchContextSrvElementCount{};
			std::uint32_t mBonePaletteSrvElementCount{};
			std::uint32_t mDrawRecordSrvElementCount{};
			std::array<std::uint32_t, Game::RFD::ShadowCascadeMaxCount> mShadowModelContextSrvElementCounts{};
			std::array<std::uint32_t, Game::RFD::ShadowCascadeMaxCount> mShadowTerrainPatchContextSrvElementCounts{};
			std::array<std::uint32_t, Game::RFD::ShadowCascadeMaxCount> mShadowDrawRecordSrvElementCounts{};

			GraphicsVector mFrameGlobalsVector{};
			GraphicsVector mShadowFrameGlobalsVector{};
			GraphicsVector mShadowMappingParameterVector{};
			GraphicsVector mModelContextVector{};
			GraphicsVector mBoundingBoxContextVector{};
			GraphicsVector mDebugGeometryContextVector{};
			GraphicsVector mTerrainPatchContextVector{};
			GraphicsVector mBonePaletteVector{};
			GraphicsVector mDrawRecordVector{};
			std::array<GraphicsVector, Game::RFD::ShadowCascadeMaxCount> mShadowModelContextVectors{};
			std::array<GraphicsVector, Game::RFD::ShadowCascadeMaxCount> mShadowTerrainPatchContextVectors{};
			std::array<GraphicsVector, Game::RFD::ShadowCascadeMaxCount> mShadowDrawRecordVectors{};

			Interface::Future mCopyFuture{};
			std::vector<DrawRecordGPU> mDrawRecordsGpu{};
			std::array<std::vector<DrawRecordGPU>, Game::RFD::ShadowCascadeMaxCount> mShadowDrawRecordsGpu{};
		};
	}
}
