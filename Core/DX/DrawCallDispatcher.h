#pragma once
#include <cstdint>
#include <map>
#include <utility>
#include <vector>
#include "Core/DX/DesciptorHeap.h"
#include "Game/Base/RenderFrameData.h"
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
			void DrawGBuffer(ID3D12GraphicsCommandList* CommandList, Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);
			void DrawEnvironmentGBuffer(ID3D12GraphicsCommandList* CommandList, const Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle EnvironmentInstanceContextSrvHandle, DescriptorHandle EnvironmentSegmentContextSrvHandle, DescriptorHandle EnvironmentDrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);
			void DrawDeferredLighting(ID3D12GraphicsCommandList* CommandList, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ShadowMappingParameterSrvHandle, DescriptorHandle ShadowMapTextureBaseSrvHandle, DescriptorHandle GBufferAlbedoSrvHandle, DescriptorHandle GBufferNormalSrvHandle, DescriptorHandle GBufferWorldPositionSrvHandle);
			void DrawSkyDome(ID3D12GraphicsCommandList* CommandList, const Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);
			void DrawForwardOverlays(ID3D12GraphicsCommandList* CommandList, const Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle BoundingBoxContextSrvHandle, DescriptorHandle DebugGeometryContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);
			void DrawDepthOnly(ID3D12GraphicsCommandList* CommandList, ID3D12GraphicsCommandList9* DynamicDepthBiasCommandList, float RasterDepthBias, float RasterDepthBiasClamp, float RasterSlopeScaledDepthBias, const Game::RFD::ShadowRenderContext& ShadowRenderContext, std::uint32_t ShadowFrameGlobalsIndex, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);
			void DrawEnvironmentDepthOnly(ID3D12GraphicsCommandList* CommandList, ID3D12GraphicsCommandList9* DynamicDepthBiasCommandList, float RasterDepthBias, float RasterDepthBiasClamp, float RasterSlopeScaledDepthBias, const Game::RFD::ShadowRenderContext& ShadowRenderContext, std::uint32_t ShadowFrameGlobalsIndex, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle EnvironmentInstanceContextSrvHandle, DescriptorHandle EnvironmentSegmentContextSrvHandle, DescriptorHandle EnvironmentDrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);

		private:
			const std::vector<D3D12_VERTEX_BUFFER_VIEW>& ResolveVertexBufferViews(const Interface::IPipeline& Pipeline, const Interface::IModelNode& Mesh);
			bool IsSkyDomePipeline(const Interface::IPipeline* Pipeline);
			const Interface::IPipeline* ResolveDepthOnlyPipeline(const Game::RFD::DrawRecord& DrawRecord);
			const Interface::IPipeline* ResolveEnvironmentDepthOnlyPipeline(const Game::RFD::EnvironmentDrawRecord& DrawRecord);
			void DrawBoundingBoxes(ID3D12GraphicsCommandList* CommandList, const Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle BoundingBoxContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);
			void DrawDebugGeometries(ID3D12GraphicsCommandList* CommandList, const Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle DebugGeometryContextSrvHandle);

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
			std::map<std::pair<const Interface::IPipeline*, const Interface::IModelNode*>, std::vector<D3D12_VERTEX_BUFFER_VIEW>> mVertexBufferViewCache{};
		};
	}
}
