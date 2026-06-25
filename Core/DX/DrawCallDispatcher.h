#pragma once
#include <cstdint>
#include <map>
#include <utility>
#include <vector>
#include <wrl/client.h>
#include "Core/DX/DesciptorHeap.h"
#include "RenderContract/Frame/RenderFrameData.h"
#include "Game/Base/Pipeline.h"
#include "Utility/DirectXInclude.h"

namespace Core {
	namespace DX {
		class DrawCallDispatcher {
		public:
			DrawCallDispatcher();
			~DrawCallDispatcher();
			DrawCallDispatcher(const DrawCallDispatcher& Other) = delete;
			DrawCallDispatcher& operator=(const DrawCallDispatcher& Other) = delete;
			DrawCallDispatcher(DrawCallDispatcher&& Other) = delete;
			DrawCallDispatcher& operator=(DrawCallDispatcher&& Other) = delete;

		public:
			void DrawGBuffer(ID3D12GraphicsCommandList* CommandList, RenderContract::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);
			void DrawEnvironmentGBuffer(ID3D12GraphicsCommandList* CommandList, const RenderContract::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle EnvironmentInstanceContextSrvHandle, DescriptorHandle EnvironmentSegmentContextSrvHandle, DescriptorHandle EnvironmentDrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);
			void DrawDeferredLighting(ID3D12GraphicsCommandList* CommandList, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ShadowMappingParameterSrvHandle, DescriptorHandle ShadowMapTextureBaseSrvHandle, DescriptorHandle GBufferAlbedoSrvHandle, DescriptorHandle GBufferNormalSrvHandle, DescriptorHandle GBufferWorldPositionSrvHandle);
			void DrawSkyDome(ID3D12GraphicsCommandList* CommandList, const RenderContract::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);
			void DrawForwardOverlays(ID3D12GraphicsCommandList* CommandList, const RenderContract::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle BoundingBoxContextSrvHandle, DescriptorHandle DebugGeometryContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);
			void DrawDepthOnly(ID3D12GraphicsCommandList* CommandList, ID3D12GraphicsCommandList9* DynamicDepthBiasCommandList, float RasterDepthBias, float RasterDepthBiasClamp, float RasterSlopeScaledDepthBias, const RenderContract::ShadowRenderContext& ShadowRenderContext, std::uint32_t ShadowFrameGlobalsIndex, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);
			void DrawEnvironmentDepthOnly(ID3D12GraphicsCommandList* CommandList, ID3D12GraphicsCommandList9* DynamicDepthBiasCommandList, float RasterDepthBias, float RasterDepthBiasClamp, float RasterSlopeScaledDepthBias, const RenderContract::ShadowRenderContext& ShadowRenderContext, std::uint32_t ShadowFrameGlobalsIndex, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle EnvironmentInstanceContextSrvHandle, DescriptorHandle EnvironmentSegmentContextSrvHandle, DescriptorHandle EnvironmentDrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);

		private:
			bool HasVertexInputBinding(const RenderContract::IPipeline& Pipeline, Game::VertexAttributeKind Kind) const;
			std::vector<D3D12_VERTEX_BUFFER_VIEW> BuildVertexBufferViews(const RenderContract::IPipeline& Pipeline, const RenderContract::IModelNode& Mesh) const;

			const std::vector<D3D12_VERTEX_BUFFER_VIEW>& ResolveVertexBufferViews(const RenderContract::IPipeline& Pipeline, const RenderContract::IModelNode& Mesh);
			bool IsSkyDomePipeline(const RenderContract::IPipeline* Pipeline);
			bool EnsureDrawIndexedIndirectCommandSignature(ID3D12GraphicsCommandList* CommandList);
			const RenderContract::IPipeline* ResolveDepthOnlyPipeline(const RenderContract::DrawRecord& DrawRecord);
			const RenderContract::IPipeline* ResolveEnvironmentDepthOnlyPipeline(const RenderContract::EnvironmentDrawRecord& DrawRecord);
			void DrawEnvironmentGBufferIndirect(ID3D12GraphicsCommandList* CommandList, const RenderContract::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);
			void DrawBoundingBoxes(ID3D12GraphicsCommandList* CommandList, const RenderContract::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle BoundingBoxContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);
			void DrawDebugGeometries(ID3D12GraphicsCommandList* CommandList, const RenderContract::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle DebugGeometryContextSrvHandle);

		private:
			Game::Base::Pipeline mDeferredLightingPipeline{};
			bool mIsDeferredLightingPipelineInitialized{};
			Game::Base::Pipeline mSkyDomePipeline{};
			bool mIsSkyDomePipelineInitialized{};
			Game::Base::Pipeline mDefaultDepthPipeline{};
			bool mIsDefaultDepthPipelineInitialized{};
			Game::Base::Pipeline mDepthAlphaCutoffPipeline{};
			bool mIsDepthAlphaCutoffPipelineInitialized{};
			Game::Base::Pipeline mSkinnedDepthPipeline{};
			bool mIsSkinnedDepthPipelineInitialized{};
			Game::Base::Pipeline mBoundingBoxLinePipeline{};
			bool mIsBoundingBoxLinePipelineInitialized{};
			Game::Base::Pipeline mDebugGeometryPipeline{};
			bool mIsDebugGeometryPipelineInitialized{};
			Game::Base::Pipeline mTerrainDepthPipeline{};
			bool mIsTerrainDepthPipelineInitialized{};
			Game::Base::Pipeline mEnvironmentObjectPipeline{};
			bool mIsEnvironmentObjectPipelineInitialized{};
			Game::Base::Pipeline mEnvironmentObjectDepthPipeline{};
			bool mIsEnvironmentObjectDepthPipelineInitialized{};
			Game::Base::Pipeline mEnvironmentBillboardDepthPipeline{};
			bool mIsEnvironmentBillboardDepthPipelineInitialized{};
			Microsoft::WRL::ComPtr<ID3D12CommandSignature> mDrawIndexedIndirectCommandSignature{};
			std::map<std::pair<const RenderContract::IPipeline*, const RenderContract::IModelNode*>, std::vector<D3D12_VERTEX_BUFFER_VIEW>> mVertexBufferViewCache{};
		};
	}
}
