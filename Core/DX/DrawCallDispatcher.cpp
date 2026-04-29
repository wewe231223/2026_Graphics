#include "DrawCallDispatcher.h"
#include <vector>

namespace Core {
	namespace DX {
		namespace {
			constexpr uint32_t InvalidDescriptorIndex{ 0xffffffffu };

			struct DrawRootConstantsB1 {
				uint32_t FrameGlobalsSrvIndex{ 0 };
				uint32_t ModelContextSrvIndex{ 0 };
				uint32_t BonePaletteSrvIndex{ 0 };
				uint32_t DrawRecordSrvIndex{ 0 };
				uint32_t DrawRecordBaseIndex{ 0 };
				uint32_t MaterialSrvIndex{ 0 };
				uint32_t MaterialTextureTableSrvIndex{ 0 };
				uint32_t ShadowMappingParameterSrvIndex{ 0 };
				uint32_t ShadowMapTextureBaseSrvIndex{ 0 };
				uint32_t FrameGlobalsElementIndex{ 0 };
				uint32_t TerrainPatchContextSrvIndex{ 0 };
				uint32_t Reserved1{ 0 };
			};

			bool HasVertexInputBinding(const Interface::IPipeline& Pipeline, Game::VertexAttributeKind Kind) {
				const std::span<const Game::VertexInputBinding> VertexInputBindings{ Pipeline.GetVertexInputBindings() };
				for (const Game::VertexInputBinding& VertexInputBinding : VertexInputBindings) {
					if (VertexInputBinding.Kind == Kind) {
						return true;
					}
				}

				return false;
			}

			std::vector<D3D12_VERTEX_BUFFER_VIEW> BuildVertexBufferViews(const Interface::IPipeline& Pipeline, const Interface::IModelNode& Mesh) {
				const std::span<const Game::VertexInputBinding> VertexInputBindings{ Pipeline.GetVertexInputBindings() };
				std::uint32_t MaxInputSlot{ 0 };

				for (const Game::VertexInputBinding& VertexInputBinding : VertexInputBindings) {
					if (VertexInputBinding.InputSlot > MaxInputSlot) {
						MaxInputSlot = VertexInputBinding.InputSlot;
					}
				}

				std::vector<D3D12_VERTEX_BUFFER_VIEW> VertexBufferViews{};
				VertexBufferViews.resize(VertexInputBindings.empty() == true ? 0 : static_cast<std::size_t>(MaxInputSlot + 1));

				for (const Game::VertexInputBinding& VertexInputBinding : VertexInputBindings) {
					D3D12_VERTEX_BUFFER_VIEW View{};
					const bool IsResolved{ Mesh.TryGetVertexBufferView(VertexInputBinding.Kind, View) };
					if (IsResolved == false) {
						continue;
					}

					VertexBufferViews[VertexInputBinding.InputSlot] = View;
				}

				return VertexBufferViews;
			}
		}

		DrawCallDispatcher::DrawCallDispatcher()
			: mDeferredLightingPipeline{},
			mIsDeferredLightingPipelineInitialized{},
			mSkyDomePipeline{},
			mIsSkyDomePipelineInitialized{},
			mDefaultDepthPipeline{},
			mIsDefaultDepthPipelineInitialized{},
			mSkinnedDepthPipeline{},
			mIsSkinnedDepthPipelineInitialized{},
			mBoundingBoxLinePipeline{},
			mIsBoundingBoxLinePipelineInitialized{},
			mDebugGeometryPipeline{},
			mIsDebugGeometryPipelineInitialized{},
			mTerrainDepthPipeline{},
			mIsTerrainDepthPipelineInitialized{} {
		}

		DrawCallDispatcher::~DrawCallDispatcher() {
		}

		void DrawCallDispatcher::DrawGBuffer(ID3D12GraphicsCommandList* CommandList, Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle) {
			const Interface::IPipeline* ActivePipeline{ nullptr };
			size_t DrawRecordIndex{ 0 };

			while (DrawRecordIndex < Data.drawRecords.size()) {
				Game::RFD::DrawRecord& StartRecord{ Data.drawRecords[DrawRecordIndex] };
				if (StartRecord.pso == nullptr || StartRecord.mesh == nullptr || DrawCallDispatcher::IsSkyDomePipeline(StartRecord.pso) == true) {
					DrawRecordIndex += 1;
					continue;
				}

				size_t RunEndIndex{ DrawRecordIndex + 1 };
				while (RunEndIndex < Data.drawRecords.size()) {
					const Game::RFD::DrawRecord& NextRecord{ Data.drawRecords[RunEndIndex] };
					bool IsSameRun{ NextRecord.pass == StartRecord.pass && NextRecord.pso == StartRecord.pso && NextRecord.mesh == StartRecord.mesh && NextRecord.submesh == StartRecord.submesh };
					if (IsSameRun == false) {
						break;
					}
					RunEndIndex += 1;
				}

				ActivePipeline = StartRecord.pso->Set(ActivePipeline, CommandList);

				DrawRootConstantsB1 RootConstants{};
				RootConstants.FrameGlobalsSrvIndex = FrameGlobalsSrvHandle.GetIndex();
				RootConstants.ModelContextSrvIndex = ModelContextSrvHandle.GetIndex();
				RootConstants.BonePaletteSrvIndex = BonePaletteSrvHandle.GetIndex();
				RootConstants.DrawRecordSrvIndex = DrawRecordSrvHandle.GetIndex();
				RootConstants.DrawRecordBaseIndex = static_cast<uint32_t>(DrawRecordIndex);
				RootConstants.MaterialSrvIndex = MaterialSrvHandle.GetIndex();
				RootConstants.MaterialTextureTableSrvIndex = MaterialTextureTableSrvHandle.GetIndex();
				RootConstants.ShadowMappingParameterSrvIndex = InvalidDescriptorIndex;
				RootConstants.ShadowMapTextureBaseSrvIndex = InvalidDescriptorIndex;
				RootConstants.FrameGlobalsElementIndex = 0u;
				RootConstants.TerrainPatchContextSrvIndex = TerrainPatchContextSrvHandle.GetIndex();
				RootConstants.Reserved1 = 0u;
				CommandList->SetGraphicsRoot32BitConstants(0, sizeof(DrawRootConstantsB1) / sizeof(uint32_t), &RootConstants, 0);

				CommandList->IASetPrimitiveTopology(StartRecord.pso->GetPrimitiveTopology());

				const std::vector<D3D12_VERTEX_BUFFER_VIEW> VertexBufferViews{ BuildVertexBufferViews(*StartRecord.pso, *StartRecord.mesh) };
				if (VertexBufferViews.empty() == false) {
					CommandList->IASetVertexBuffers(0, static_cast<UINT>(VertexBufferViews.size()), VertexBufferViews.data());
				}

				const D3D12_INDEX_BUFFER_VIEW& IndexBufferView{ StartRecord.mesh->GetIndexBufferView() };
				CommandList->IASetIndexBuffer(&IndexBufferView);

				const Game::ModelSubMesh& SubMesh{ StartRecord.mesh->GetSubMesh(StartRecord.submesh) };
				UINT IndexCountPerInstance{ static_cast<UINT>(SubMesh.IndexCount) };
				UINT InstanceCount{ static_cast<UINT>(RunEndIndex - DrawRecordIndex) };
				UINT StartIndexLocation{ static_cast<UINT>(SubMesh.IndexOffset) };
				INT BaseVertexLocation{ 0 };
				UINT StartInstanceLocation{ 0 };
				CommandList->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
				DrawRecordIndex = RunEndIndex;
			}
		}

		void DrawCallDispatcher::DrawDeferredLighting(ID3D12GraphicsCommandList* CommandList, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ShadowMappingParameterSrvHandle, DescriptorHandle ShadowMapTextureBaseSrvHandle, DescriptorHandle GBufferAlbedoSrvHandle, DescriptorHandle GBufferNormalSrvHandle, DescriptorHandle GBufferWorldPositionSrvHandle) {
			if (CommandList == nullptr) {
				return;
			}

			if (mIsDeferredLightingPipelineInitialized == false) {
				mIsDeferredLightingPipelineInitialized = mDeferredLightingPipeline.Initialize("DeferredLightingGraphics");
			}

			if (mIsDeferredLightingPipelineInitialized == false) {
				return;
			}

			mDeferredLightingPipeline.Set(nullptr, CommandList);

			DrawRootConstantsB1 RootConstants{};
			RootConstants.FrameGlobalsSrvIndex = FrameGlobalsSrvHandle.GetIndex();
			RootConstants.ModelContextSrvIndex = GBufferAlbedoSrvHandle.GetIndex();
			RootConstants.BonePaletteSrvIndex = GBufferNormalSrvHandle.GetIndex();
			RootConstants.DrawRecordSrvIndex = GBufferWorldPositionSrvHandle.GetIndex();
			RootConstants.DrawRecordBaseIndex = 0u;
			RootConstants.MaterialSrvIndex = InvalidDescriptorIndex;
			RootConstants.MaterialTextureTableSrvIndex = InvalidDescriptorIndex;
			RootConstants.ShadowMappingParameterSrvIndex = ShadowMappingParameterSrvHandle.GetIndex();
			RootConstants.ShadowMapTextureBaseSrvIndex = ShadowMapTextureBaseSrvHandle.GetIndex();
			RootConstants.FrameGlobalsElementIndex = 0u;
			RootConstants.TerrainPatchContextSrvIndex = InvalidDescriptorIndex;
			RootConstants.Reserved1 = 0u;
			CommandList->SetGraphicsRoot32BitConstants(0, sizeof(DrawRootConstantsB1) / sizeof(uint32_t), &RootConstants, 0);

			CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			CommandList->DrawInstanced(3u, 1u, 0u, 0u);
		}

		void DrawCallDispatcher::DrawForwardOverlays(ID3D12GraphicsCommandList* CommandList, const Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle BoundingBoxContextSrvHandle, DescriptorHandle DebugGeometryContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle) {
			DrawBoundingBoxes(CommandList, Data, FrameGlobalsSrvHandle, BoundingBoxContextSrvHandle, BonePaletteSrvHandle, DrawRecordSrvHandle, MaterialSrvHandle, MaterialTextureTableSrvHandle);
			DrawDebugGeometries(CommandList, Data, FrameGlobalsSrvHandle, DebugGeometryContextSrvHandle);
		}

		void DrawCallDispatcher::DrawDepthOnly(ID3D12GraphicsCommandList* CommandList, const Game::RFD::ShadowRenderContext& ShadowRenderContext, std::uint32_t ShadowFrameGlobalsIndex, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle) {
			const Interface::IPipeline* ActivePipeline{ nullptr };
			size_t DrawRecordIndex{ 0 };

			while (DrawRecordIndex < ShadowRenderContext.DrawRecords.size()) {
				const Game::RFD::DrawRecord& StartRecord{ ShadowRenderContext.DrawRecords[DrawRecordIndex] };
				if (StartRecord.pso == nullptr || StartRecord.mesh == nullptr || DrawCallDispatcher::IsSkyDomePipeline(StartRecord.pso) == true) {
					DrawRecordIndex += 1;
					continue;
				}

				size_t RunEndIndex{ DrawRecordIndex + 1 };
				while (RunEndIndex < ShadowRenderContext.DrawRecords.size()) {
					const Game::RFD::DrawRecord& NextRecord{ ShadowRenderContext.DrawRecords[RunEndIndex] };
					bool IsSameRun{ NextRecord.pass == StartRecord.pass && NextRecord.pso == StartRecord.pso && NextRecord.mesh == StartRecord.mesh && NextRecord.submesh == StartRecord.submesh };
					if (IsSameRun == false) {
						break;
					}
					RunEndIndex += 1;
				}

				const Interface::IPipeline* DepthPipeline{ DrawCallDispatcher::ResolveDepthOnlyPipeline(StartRecord) };
				if (DepthPipeline == nullptr) {
					DrawRecordIndex = RunEndIndex;
					continue;
				}

				ActivePipeline = DepthPipeline->Set(ActivePipeline, CommandList);

				DrawRootConstantsB1 RootConstants{};
				RootConstants.FrameGlobalsSrvIndex = FrameGlobalsSrvHandle.GetIndex();
				RootConstants.ModelContextSrvIndex = ModelContextSrvHandle.GetIndex();
				RootConstants.BonePaletteSrvIndex = BonePaletteSrvHandle.GetIndex();
				RootConstants.DrawRecordSrvIndex = DrawRecordSrvHandle.GetIndex();
				RootConstants.DrawRecordBaseIndex = static_cast<uint32_t>(DrawRecordIndex);
				RootConstants.MaterialSrvIndex = MaterialSrvHandle.GetIndex();
				RootConstants.MaterialTextureTableSrvIndex = MaterialTextureTableSrvHandle.GetIndex();
				RootConstants.ShadowMappingParameterSrvIndex = InvalidDescriptorIndex;
				RootConstants.ShadowMapTextureBaseSrvIndex = InvalidDescriptorIndex;
				RootConstants.FrameGlobalsElementIndex = ShadowFrameGlobalsIndex;
				RootConstants.TerrainPatchContextSrvIndex = TerrainPatchContextSrvHandle.GetIndex();
				RootConstants.Reserved1 = 0u;
				CommandList->SetGraphicsRoot32BitConstants(0, sizeof(DrawRootConstantsB1) / sizeof(uint32_t), &RootConstants, 0);

				CommandList->IASetPrimitiveTopology(DepthPipeline->GetPrimitiveTopology());

				const std::vector<D3D12_VERTEX_BUFFER_VIEW> VertexBufferViews{ BuildVertexBufferViews(*DepthPipeline, *StartRecord.mesh) };
				if (VertexBufferViews.empty() == false) {
					CommandList->IASetVertexBuffers(0, static_cast<UINT>(VertexBufferViews.size()), VertexBufferViews.data());
				}

				const D3D12_INDEX_BUFFER_VIEW& IndexBufferView{ StartRecord.mesh->GetIndexBufferView() };
				CommandList->IASetIndexBuffer(&IndexBufferView);

				const Game::ModelSubMesh& SubMesh{ StartRecord.mesh->GetSubMesh(StartRecord.submesh) };
				UINT IndexCountPerInstance{ static_cast<UINT>(SubMesh.IndexCount) };
				UINT InstanceCount{ static_cast<UINT>(RunEndIndex - DrawRecordIndex) };
				UINT StartIndexLocation{ static_cast<UINT>(SubMesh.IndexOffset) };
				INT BaseVertexLocation{ 0 };
				UINT StartInstanceLocation{ 0 };
				CommandList->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
				DrawRecordIndex = RunEndIndex;
			}
		}

		bool DrawCallDispatcher::IsSkyDomePipeline(const Interface::IPipeline* Pipeline) {
			if (Pipeline == nullptr) {
				return false;
			}

			if (mIsSkyDomePipelineInitialized == false) {
				mIsSkyDomePipelineInitialized = mSkyDomePipeline.Initialize("SkyDomeGraphics");
			}

			if (mIsSkyDomePipelineInitialized == false) {
				return false;
			}

			return Pipeline->Get() == mSkyDomePipeline.Get();
		}

		const Interface::IPipeline* DrawCallDispatcher::ResolveDepthOnlyPipeline(const Game::RFD::DrawRecord& DrawRecord) {
			if (DrawRecord.pso == nullptr) {
				return nullptr;
			}

			if (DrawRecord.TerrainPatchContextIndex != InvalidDescriptorIndex) {
				if (mIsTerrainDepthPipelineInitialized == false) {
					mIsTerrainDepthPipelineInitialized = mTerrainDepthPipeline.Initialize("TerrainDepthGraphics");
				}

				return mIsTerrainDepthPipelineInitialized == true ? &mTerrainDepthPipeline : nullptr;
			}

			const bool HasSkinningInput{ HasVertexInputBinding(*DrawRecord.pso, Game::VertexAttributeKind::BoneIndices) || HasVertexInputBinding(*DrawRecord.pso, Game::VertexAttributeKind::BoneWeights) };
			if (HasSkinningInput == true) {
				if (mIsSkinnedDepthPipelineInitialized == false) {
					mIsSkinnedDepthPipelineInitialized = mSkinnedDepthPipeline.Initialize("SkinnedDepthGraphics");
				}

				return mIsSkinnedDepthPipelineInitialized == true ? &mSkinnedDepthPipeline : nullptr;
			}

			if (mIsDefaultDepthPipelineInitialized == false) {
				mIsDefaultDepthPipelineInitialized = mDefaultDepthPipeline.Initialize("DefaultDepthGraphics");
			}

			return mIsDefaultDepthPipelineInitialized == true ? &mDefaultDepthPipeline : nullptr;
		}

		void DrawCallDispatcher::DrawSkyDome(ID3D12GraphicsCommandList* CommandList, const Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle) {
			if (CommandList == nullptr) {
				return;
			}

			if (mIsSkyDomePipelineInitialized == false) {
				mIsSkyDomePipelineInitialized = mSkyDomePipeline.Initialize("SkyDomeGraphics");
			}

			if (mIsSkyDomePipelineInitialized == false) {
				return;
			}

			const Interface::IPipeline* ActivePipeline{ nullptr };
			size_t DrawRecordIndex{ 0 };

			while (DrawRecordIndex < Data.drawRecords.size()) {
				const Game::RFD::DrawRecord& StartRecord{ Data.drawRecords[DrawRecordIndex] };
				if (StartRecord.pso == nullptr || StartRecord.mesh == nullptr || DrawCallDispatcher::IsSkyDomePipeline(StartRecord.pso) == false) {
					DrawRecordIndex += 1;
					continue;
				}

				size_t RunEndIndex{ DrawRecordIndex + 1 };
				while (RunEndIndex < Data.drawRecords.size()) {
					const Game::RFD::DrawRecord& NextRecord{ Data.drawRecords[RunEndIndex] };
					bool IsSameRun{ NextRecord.pass == StartRecord.pass && NextRecord.pso == StartRecord.pso && NextRecord.mesh == StartRecord.mesh && NextRecord.submesh == StartRecord.submesh };
					if (IsSameRun == false) {
						break;
					}
					RunEndIndex += 1;
				}

				ActivePipeline = StartRecord.pso->Set(ActivePipeline, CommandList);

				DrawRootConstantsB1 RootConstants{};
				RootConstants.FrameGlobalsSrvIndex = FrameGlobalsSrvHandle.GetIndex();
				RootConstants.ModelContextSrvIndex = ModelContextSrvHandle.GetIndex();
				RootConstants.BonePaletteSrvIndex = BonePaletteSrvHandle.GetIndex();
				RootConstants.DrawRecordSrvIndex = DrawRecordSrvHandle.GetIndex();
				RootConstants.DrawRecordBaseIndex = static_cast<uint32_t>(DrawRecordIndex);
				RootConstants.MaterialSrvIndex = MaterialSrvHandle.GetIndex();
				RootConstants.MaterialTextureTableSrvIndex = MaterialTextureTableSrvHandle.GetIndex();
				RootConstants.ShadowMappingParameterSrvIndex = InvalidDescriptorIndex;
				RootConstants.ShadowMapTextureBaseSrvIndex = InvalidDescriptorIndex;
				RootConstants.FrameGlobalsElementIndex = 0u;
				RootConstants.TerrainPatchContextSrvIndex = TerrainPatchContextSrvHandle.GetIndex();
				RootConstants.Reserved1 = 0u;
				CommandList->SetGraphicsRoot32BitConstants(0, sizeof(DrawRootConstantsB1) / sizeof(uint32_t), &RootConstants, 0);

				CommandList->IASetPrimitiveTopology(StartRecord.pso->GetPrimitiveTopology());

				const std::vector<D3D12_VERTEX_BUFFER_VIEW> VertexBufferViews{ BuildVertexBufferViews(*StartRecord.pso, *StartRecord.mesh) };
				if (VertexBufferViews.empty() == false) {
					CommandList->IASetVertexBuffers(0, static_cast<UINT>(VertexBufferViews.size()), VertexBufferViews.data());
				}

				const D3D12_INDEX_BUFFER_VIEW& IndexBufferView{ StartRecord.mesh->GetIndexBufferView() };
				CommandList->IASetIndexBuffer(&IndexBufferView);

				const Game::ModelSubMesh& SubMesh{ StartRecord.mesh->GetSubMesh(StartRecord.submesh) };
				UINT IndexCountPerInstance{ static_cast<UINT>(SubMesh.IndexCount) };
				UINT InstanceCount{ static_cast<UINT>(RunEndIndex - DrawRecordIndex) };
				UINT StartIndexLocation{ static_cast<UINT>(SubMesh.IndexOffset) };
				INT BaseVertexLocation{ 0 };
				UINT StartInstanceLocation{ 0 };
				CommandList->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
				DrawRecordIndex = RunEndIndex;
			}
		}

		void DrawCallDispatcher::DrawBoundingBoxes(ID3D12GraphicsCommandList* CommandList, const Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle BoundingBoxContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle) {
			if (CommandList == nullptr) {
				return;
			}

			const bool IsDrawBoundingBoxesEnabled{ (Data.globals.flags & Game::RFD::FrameGlobalFlagDrawBoundingBoxes) != 0u };
			if (IsDrawBoundingBoxesEnabled == false || Data.boundingBoxContexts.empty()) {
				return;
			}

			if (mIsBoundingBoxLinePipelineInitialized == false) {
				mIsBoundingBoxLinePipelineInitialized = mBoundingBoxLinePipeline.Initialize("BoundingBoxLineGraphics");
			}

			if (mIsBoundingBoxLinePipelineInitialized == false) {
				return;
			}

			mBoundingBoxLinePipeline.Set(nullptr, CommandList);

			DrawRootConstantsB1 RootConstants{};
			RootConstants.FrameGlobalsSrvIndex = FrameGlobalsSrvHandle.GetIndex();
			RootConstants.ModelContextSrvIndex = BoundingBoxContextSrvHandle.GetIndex();
			RootConstants.BonePaletteSrvIndex = BonePaletteSrvHandle.GetIndex();
			RootConstants.DrawRecordSrvIndex = DrawRecordSrvHandle.GetIndex();
			RootConstants.DrawRecordBaseIndex = 0u;
			RootConstants.MaterialSrvIndex = MaterialSrvHandle.GetIndex();
			RootConstants.MaterialTextureTableSrvIndex = MaterialTextureTableSrvHandle.GetIndex();
			RootConstants.ShadowMappingParameterSrvIndex = InvalidDescriptorIndex;
			RootConstants.ShadowMapTextureBaseSrvIndex = InvalidDescriptorIndex;
			RootConstants.FrameGlobalsElementIndex = 0u;
			RootConstants.TerrainPatchContextSrvIndex = InvalidDescriptorIndex;
			RootConstants.Reserved1 = 0u;
			CommandList->SetGraphicsRoot32BitConstants(0, sizeof(DrawRootConstantsB1) / sizeof(uint32_t), &RootConstants, 0);

			CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
			CommandList->DrawInstanced(1u, static_cast<UINT>(Data.boundingBoxContexts.size()), 0u, 0u);
		}

		void DrawCallDispatcher::DrawDebugGeometries(ID3D12GraphicsCommandList* CommandList, const Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle DebugGeometryContextSrvHandle) {
			if (CommandList == nullptr) {
				return;
			}

			const bool IsDrawDebugGeometriesEnabled{ (Data.globals.flags & Game::RFD::FrameGlobalFlagDrawDebugGeometry) != 0u };
			if (IsDrawDebugGeometriesEnabled == false || Data.debugGeometryContexts.empty()) {
				return;
			}

			if (mIsDebugGeometryPipelineInitialized == false) {
				mIsDebugGeometryPipelineInitialized = mDebugGeometryPipeline.Initialize("DebugGeometryGraphics");
			}

			if (mIsDebugGeometryPipelineInitialized == false) {
				return;
			}

			mDebugGeometryPipeline.Set(nullptr, CommandList);

			DrawRootConstantsB1 RootConstants{};
			RootConstants.FrameGlobalsSrvIndex = FrameGlobalsSrvHandle.GetIndex();
			RootConstants.ModelContextSrvIndex = DebugGeometryContextSrvHandle.GetIndex();
			RootConstants.BonePaletteSrvIndex = InvalidDescriptorIndex;
			RootConstants.DrawRecordSrvIndex = InvalidDescriptorIndex;
			RootConstants.DrawRecordBaseIndex = 0u;
			RootConstants.MaterialSrvIndex = InvalidDescriptorIndex;
			RootConstants.MaterialTextureTableSrvIndex = InvalidDescriptorIndex;
			RootConstants.ShadowMappingParameterSrvIndex = InvalidDescriptorIndex;
			RootConstants.ShadowMapTextureBaseSrvIndex = InvalidDescriptorIndex;
			RootConstants.FrameGlobalsElementIndex = 0u;
			RootConstants.TerrainPatchContextSrvIndex = InvalidDescriptorIndex;
			RootConstants.Reserved1 = 0u;
			CommandList->SetGraphicsRoot32BitConstants(0, sizeof(DrawRootConstantsB1) / sizeof(uint32_t), &RootConstants, 0);

			CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
			CommandList->DrawInstanced(1u, static_cast<UINT>(Data.debugGeometryContexts.size()), 0u, 0u);
		}
	}
}
