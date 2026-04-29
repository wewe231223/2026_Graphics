#pragma once
#include <cstdint>
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
			void DrawDeferredLighting(ID3D12GraphicsCommandList* CommandList, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ShadowMappingParameterSrvHandle, DescriptorHandle ShadowMapTextureBaseSrvHandle, DescriptorHandle GBufferAlbedoSrvHandle, DescriptorHandle GBufferNormalSrvHandle, DescriptorHandle GBufferWorldPositionSrvHandle);
			void DrawForwardOverlays(ID3D12GraphicsCommandList* CommandList, const Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle BoundingBoxContextSrvHandle, DescriptorHandle DebugGeometryContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);
			void DrawDepthOnly(ID3D12GraphicsCommandList* CommandList, const Game::RFD::ShadowRenderContext& ShadowRenderContext, std::uint32_t ShadowFrameGlobalsIndex, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);

		private:
			bool IsSkyDomePipeline(const Interface::IPipeline* Pipeline);
			void DrawSkyDome(ID3D12GraphicsCommandList* CommandList, const Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);
			const Interface::IPipeline* ResolveDepthOnlyPipeline(const Game::RFD::DrawRecord& DrawRecord);
			void DrawBoundingBoxes(ID3D12GraphicsCommandList* CommandList, const Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle BoundingBoxContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle);
			void DrawDebugGeometries(ID3D12GraphicsCommandList* CommandList, const Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle DebugGeometryContextSrvHandle);

		private:
			Game::Base::Pipeline mDeferredLightingPipeline{};
			bool mIsDeferredLightingPipelineInitialized{};
			Game::Base::Pipeline mSkyDomePipeline{};
			bool mIsSkyDomePipelineInitialized{};
			Game::Base::Pipeline mDefaultDepthPipeline{};
			bool mIsDefaultDepthPipelineInitialized{};
			Game::Base::Pipeline mSkinnedDepthPipeline{};
			bool mIsSkinnedDepthPipelineInitialized{};
			Game::Base::Pipeline mBoundingBoxLinePipeline{};
			bool mIsBoundingBoxLinePipelineInitialized{};
			Game::Base::Pipeline mDebugGeometryPipeline{};
			bool mIsDebugGeometryPipelineInitialized{};
			Game::Base::Pipeline mTerrainDepthPipeline{};
			bool mIsTerrainDepthPipelineInitialized{};
		};
	}
}
