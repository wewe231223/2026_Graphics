#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "Core/Common.h"
#include "Core/DX/DesciptorHeap.h"
#include "Core/DX/GraphicsVector.h"
#include "RenderContract/Frame/RenderFrameData.h"

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
			void PrepareFrameResources(RenderContract::RenderFrameData& Data, GraphicsAllocator& GraphicsAllocator, Interface::ICopyQueue* CopyQueue);
			const RenderContract::Future& GetCopyFuture() const;

			DescriptorHandle GetFrameGlobalsSrvHandle() const;
			DescriptorHandle GetShadowFrameGlobalsSrvHandle() const;
			DescriptorHandle GetShadowMappingParameterSrvHandle() const;
			DescriptorHandle GetModelContextSrvHandle() const;
			DescriptorHandle GetBoundingBoxContextSrvHandle() const;
			DescriptorHandle GetDebugGeometryContextSrvHandle() const;
			DescriptorHandle GetTerrainPatchContextSrvHandle() const;
			DescriptorHandle GetBonePaletteSrvHandle() const;
			DescriptorHandle GetDrawRecordSrvHandle() const;
			DescriptorHandle GetEnvironmentInstanceContextSrvHandle() const;
			DescriptorHandle GetEnvironmentSegmentContextSrvHandle() const;
			DescriptorHandle GetEnvironmentDrawRecordSrvHandle() const;
			DescriptorHandle GetShadowModelContextSrvHandle(std::uint32_t ShadowCascadeIndex) const;
			DescriptorHandle GetShadowTerrainPatchContextSrvHandle(std::uint32_t ShadowCascadeIndex) const;
			DescriptorHandle GetShadowDrawRecordSrvHandle(std::uint32_t ShadowCascadeIndex) const;
			DescriptorHandle GetShadowEnvironmentDrawRecordSrvHandle(std::uint32_t ShadowCascadeIndex) const;

		private:
			static bool CompareDrawRecordByPso(const RenderContract::DrawRecord& Left, const RenderContract::DrawRecord& Right);
			static bool CompareEnvironmentDrawRecordByPso(const RenderContract::EnvironmentDrawRecord& Left, const RenderContract::EnvironmentDrawRecord& Right);
			static bool IsDrawRecordsSorted(const std::vector<RenderContract::DrawRecord>& DrawRecords);
			static bool IsEnvironmentDrawRecordsSorted(const std::vector<RenderContract::EnvironmentDrawRecord>& DrawRecords);
			static void SortShadowDrawRecords(std::vector<RenderContract::DrawRecord>& DrawRecords);
			static void SortShadowEnvironmentDrawRecords(std::vector<RenderContract::EnvironmentDrawRecord>& DrawRecords);
			void BuildDrawRecordGpu(const std::vector<RenderContract::DrawRecord>& DrawRecords, std::vector<DrawRecordGPU>& OutDrawRecordsGpu);
			void BuildEnvironmentDrawRecordGpu(const std::vector<RenderContract::EnvironmentDrawRecord>& DrawRecords, std::vector<RenderContract::EnvironmentDrawRecordGpu>& OutDrawRecordsGpu);
			void UpdateShaderResourceViews(std::uint32_t FrameGlobalsCount, std::uint32_t ShadowFrameGlobalsCount, std::uint32_t ShadowMappingParameterCount, std::uint32_t ModelContextCount, std::uint32_t BoundingBoxContextCount, std::uint32_t DebugGeometryContextCount, std::uint32_t TerrainPatchContextCount, std::uint32_t BonePaletteCount, std::uint32_t DrawRecordCount);
			void UpdateEnvironmentShaderResourceViews(std::uint32_t EnvironmentInstanceContextCount, std::uint32_t EnvironmentSegmentContextCount, std::uint32_t EnvironmentDrawRecordCount);
			void UpdateShadowShaderResourceViews(std::uint32_t ShadowCascadeCount);
			void UpdateShadowEnvironmentShaderResourceViews(std::uint32_t ShadowCascadeCount);
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
			DescriptorHandle mEnvironmentInstanceContextSrvHandle{};
			DescriptorHandle mEnvironmentSegmentContextSrvHandle{};
			DescriptorHandle mEnvironmentDrawRecordSrvHandle{};
			std::array<DescriptorHandle, RenderContract::ShadowCascadeMaxCount> mShadowModelContextSrvHandles{};
			std::array<DescriptorHandle, RenderContract::ShadowCascadeMaxCount> mShadowTerrainPatchContextSrvHandles{};
			std::array<DescriptorHandle, RenderContract::ShadowCascadeMaxCount> mShadowDrawRecordSrvHandles{};
			std::array<DescriptorHandle, RenderContract::ShadowCascadeMaxCount> mShadowEnvironmentDrawRecordSrvHandles{};

			ID3D12Resource* mFrameGlobalsSrvResource{};
			ID3D12Resource* mShadowFrameGlobalsSrvResource{};
			ID3D12Resource* mShadowMappingParameterSrvResource{};
			ID3D12Resource* mModelContextSrvResource{};
			ID3D12Resource* mBoundingBoxContextSrvResource{};
			ID3D12Resource* mDebugGeometryContextSrvResource{};
			ID3D12Resource* mTerrainPatchContextSrvResource{};
			ID3D12Resource* mBonePaletteSrvResource{};
			ID3D12Resource* mDrawRecordSrvResource{};
			ID3D12Resource* mEnvironmentInstanceContextSrvResource{};
			ID3D12Resource* mEnvironmentSegmentContextSrvResource{};
			ID3D12Resource* mEnvironmentDrawRecordSrvResource{};
			std::array<ID3D12Resource*, RenderContract::ShadowCascadeMaxCount> mShadowModelContextSrvResources{};
			std::array<ID3D12Resource*, RenderContract::ShadowCascadeMaxCount> mShadowTerrainPatchContextSrvResources{};
			std::array<ID3D12Resource*, RenderContract::ShadowCascadeMaxCount> mShadowDrawRecordSrvResources{};
			std::array<ID3D12Resource*, RenderContract::ShadowCascadeMaxCount> mShadowEnvironmentDrawRecordSrvResources{};

			std::uint32_t mFrameGlobalsSrvElementCount{};
			std::uint32_t mShadowFrameGlobalsSrvElementCount{};
			std::uint32_t mShadowMappingParameterSrvElementCount{};
			std::uint32_t mModelContextSrvElementCount{};
			std::uint32_t mBoundingBoxContextSrvElementCount{};
			std::uint32_t mDebugGeometryContextSrvElementCount{};
			std::uint32_t mTerrainPatchContextSrvElementCount{};
			std::uint32_t mBonePaletteSrvElementCount{};
			std::uint32_t mDrawRecordSrvElementCount{};
			std::uint32_t mEnvironmentInstanceContextSrvElementCount{};
			std::uint32_t mEnvironmentSegmentContextSrvElementCount{};
			std::uint32_t mEnvironmentDrawRecordSrvElementCount{};
			std::array<std::uint32_t, RenderContract::ShadowCascadeMaxCount> mShadowModelContextSrvElementCounts{};
			std::array<std::uint32_t, RenderContract::ShadowCascadeMaxCount> mShadowTerrainPatchContextSrvElementCounts{};
			std::array<std::uint32_t, RenderContract::ShadowCascadeMaxCount> mShadowDrawRecordSrvElementCounts{};
			std::array<std::uint32_t, RenderContract::ShadowCascadeMaxCount> mShadowEnvironmentDrawRecordSrvElementCounts{};

			GraphicsVector mFrameGlobalsVector{};
			GraphicsVector mShadowFrameGlobalsVector{};
			GraphicsVector mShadowMappingParameterVector{};
			GraphicsVector mModelContextVector{};
			GraphicsVector mBoundingBoxContextVector{};
			GraphicsVector mDebugGeometryContextVector{};
			GraphicsVector mTerrainPatchContextVector{};
			GraphicsVector mBonePaletteVector{};
			GraphicsVector mDrawRecordVector{};
			GraphicsVector mEnvironmentInstanceContextVector{};
			GraphicsVector mEnvironmentSegmentContextVector{};
			GraphicsVector mEnvironmentDrawRecordVector{};
			std::array<GraphicsVector, RenderContract::ShadowCascadeMaxCount> mShadowModelContextVectors{};
			std::array<GraphicsVector, RenderContract::ShadowCascadeMaxCount> mShadowTerrainPatchContextVectors{};
			std::array<GraphicsVector, RenderContract::ShadowCascadeMaxCount> mShadowDrawRecordVectors{};
			std::array<GraphicsVector, RenderContract::ShadowCascadeMaxCount> mShadowEnvironmentDrawRecordVectors{};

			RenderContract::Future mCopyFuture{};
			std::vector<RenderContract::ModelContext> mGpuModelContexts{};
			std::vector<SimpleMath::Matrix> mGpuBonePalette{};
			std::vector<RenderContract::EnvironmentSegmentContext> mGpuEnvironmentSegmentContexts{};
			std::array<std::vector<RenderContract::ModelContext>, RenderContract::ShadowCascadeMaxCount> mGpuShadowModelContexts{};
			std::vector<DrawRecordGPU> mDrawRecordsGpu{};
			std::array<std::vector<DrawRecordGPU>, RenderContract::ShadowCascadeMaxCount> mShadowDrawRecordsGpu{};
			std::vector<RenderContract::EnvironmentDrawRecordGpu> mEnvironmentDrawRecordsGpu{};
			std::array<std::vector<RenderContract::EnvironmentDrawRecordGpu>, RenderContract::ShadowCascadeMaxCount> mShadowEnvironmentDrawRecordsGpu{};
		};
	}
}
