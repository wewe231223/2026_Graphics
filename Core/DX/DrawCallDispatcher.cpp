#include "DrawCallDispatcher.h"
#include <tuple>
#include <utility>
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
		}

		bool DrawCallDispatcher::HasVertexInputBinding(const RenderContract::IPipeline& Pipeline, Game::VertexAttributeKind Kind) const {
			const std::span<const Game::VertexInputBinding> VertexInputBindings{ Pipeline.GetVertexInputBindings() };
			for (const Game::VertexInputBinding& VertexInputBinding : VertexInputBindings) {
				if (VertexInputBinding.mKind == Kind) {
					return true;
				}
			}

			return false;
		}

		std::vector<D3D12_VERTEX_BUFFER_VIEW> DrawCallDispatcher::BuildVertexBufferViews(const RenderContract::IPipeline& Pipeline, const RenderContract::IModelNode& Mesh) const {
			const std::span<const Game::VertexInputBinding> VertexInputBindings{ Pipeline.GetVertexInputBindings() };
			std::uint32_t MaxInputSlot{ 0 };

			for (const Game::VertexInputBinding& VertexInputBinding : VertexInputBindings) {
				if (VertexInputBinding.mInputSlot > MaxInputSlot) {
					MaxInputSlot = VertexInputBinding.mInputSlot;
				}
			}

			std::vector<D3D12_VERTEX_BUFFER_VIEW> VertexBufferViews{};
			VertexBufferViews.resize(VertexInputBindings.empty() == true ? 0 : static_cast<std::size_t>(MaxInputSlot + 1));

			for (const Game::VertexInputBinding& VertexInputBinding : VertexInputBindings) {
				D3D12_VERTEX_BUFFER_VIEW View{};
				const bool IsResolved{ Mesh.TryGetVertexBufferView(VertexInputBinding.mKind, View) };
				if (IsResolved == false) {
					continue;
				}

				VertexBufferViews[VertexInputBinding.mInputSlot] = View;
			}

			return VertexBufferViews;
		}

		DrawCallDispatcher::DrawCallDispatcher()
			: mDeferredLightingPipeline{},
			mIsDeferredLightingPipelineInitialized{},
			mSkyDomePipeline{},
			mIsSkyDomePipelineInitialized{},
			mDefaultDepthPipeline{},
			mIsDefaultDepthPipelineInitialized{},
			mDepthAlphaCutoffPipeline{},
			mIsDepthAlphaCutoffPipelineInitialized{},
			mSkinnedDepthPipeline{},
			mIsSkinnedDepthPipelineInitialized{},
			mBoundingBoxLinePipeline{},
			mIsBoundingBoxLinePipelineInitialized{},
			mDebugGeometryPipeline{},
			mIsDebugGeometryPipelineInitialized{},
			mTerrainDepthPipeline{},
			mIsTerrainDepthPipelineInitialized{},
			mEnvironmentObjectPipeline{},
			mIsEnvironmentObjectPipelineInitialized{},
			mEnvironmentObjectDepthPipeline{},
			mIsEnvironmentObjectDepthPipelineInitialized{},
			mEnvironmentBillboardDepthPipeline{},
			mIsEnvironmentBillboardDepthPipelineInitialized{} {
		}

		DrawCallDispatcher::~DrawCallDispatcher() {
		}

		void DrawCallDispatcher::DrawGBuffer(ID3D12GraphicsCommandList* CommandList, RenderContract::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle) {
			const RenderContract::IPipeline* ActivePipeline{ nullptr };
			size_t DrawRecordIndex{ 0 };

			while (DrawRecordIndex < Data.mDrawRecords.size()) {
				RenderContract::DrawRecord& StartRecord{ Data.mDrawRecords[DrawRecordIndex] };
				if (StartRecord.mPipeline == nullptr || StartRecord.mMesh == nullptr || DrawCallDispatcher::IsSkyDomePipeline(StartRecord.mPipeline) == true) {
					DrawRecordIndex += 1;
					continue;
				}

				size_t RunEndIndex{ DrawRecordIndex + 1 };
				while (RunEndIndex < Data.mDrawRecords.size()) {
					const RenderContract::DrawRecord& NextRecord{ Data.mDrawRecords[RunEndIndex] };
					bool IsSameRun{ std::tie(NextRecord.mPass, NextRecord.mPipeline, NextRecord.mMesh, NextRecord.mSubMesh) == std::tie(StartRecord.mPass, StartRecord.mPipeline, StartRecord.mMesh, StartRecord.mSubMesh) };
					if (IsSameRun == false) {
						break;
					}
					RunEndIndex += 1;
				}

				ActivePipeline = StartRecord.mPipeline->Set(ActivePipeline, CommandList);

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

				CommandList->IASetPrimitiveTopology(StartRecord.mPipeline->GetPrimitiveTopology());

				const std::vector<D3D12_VERTEX_BUFFER_VIEW>& VertexBufferViews{ ResolveVertexBufferViews(*StartRecord.mPipeline, *StartRecord.mMesh) };
				if (VertexBufferViews.empty() == false) {
					CommandList->IASetVertexBuffers(0, static_cast<UINT>(VertexBufferViews.size()), VertexBufferViews.data());
				}

				const D3D12_INDEX_BUFFER_VIEW& IndexBufferView{ StartRecord.mMesh->GetIndexBufferView() };
				CommandList->IASetIndexBuffer(&IndexBufferView);

				const Game::ModelSubMesh& SubMesh{ StartRecord.mMesh->GetSubMesh(StartRecord.mSubMesh) };
				UINT IndexCountPerInstance{ static_cast<UINT>(SubMesh.mIndexCount) };
				UINT InstanceCount{ static_cast<UINT>(RunEndIndex - DrawRecordIndex) };
				UINT StartIndexLocation{ static_cast<UINT>(SubMesh.mIndexOffset) };
				INT BaseVertexLocation{ 0 };
				UINT StartInstanceLocation{ 0 };
				CommandList->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
				DrawRecordIndex = RunEndIndex;
			}
		}

		void DrawCallDispatcher::DrawEnvironmentGBuffer(ID3D12GraphicsCommandList* CommandList, const RenderContract::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle EnvironmentInstanceContextSrvHandle, DescriptorHandle EnvironmentSegmentContextSrvHandle, DescriptorHandle EnvironmentDrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle) {
			if (CommandList == nullptr || Data.mEnvironmentDrawRecords.empty() == true) {
				return;
			}

			if (mIsEnvironmentObjectPipelineInitialized == false) {
				mIsEnvironmentObjectPipelineInitialized = mEnvironmentObjectPipeline.Initialize("EnvironmentObjectGraphics");
			}

			const RenderContract::IPipeline* ActivePipeline{ nullptr };
			for (std::size_t DrawRecordIndex{}; DrawRecordIndex < Data.mEnvironmentDrawRecords.size(); DrawRecordIndex += 1ULL) {
				const RenderContract::EnvironmentDrawRecord& DrawRecord{ Data.mEnvironmentDrawRecords[DrawRecordIndex] };
				if (DrawRecord.mMesh == nullptr || DrawRecord.mInstanceCount == 0u) {
					continue;
				}

				const bool IsEnvironmentBillboardPipeline{ DrawRecord.mPipeline != nullptr && DrawRecord.mPipeline->GetPrimitiveTopology() == D3D_PRIMITIVE_TOPOLOGY_POINTLIST };
				const RenderContract::IPipeline* Pipeline{ IsEnvironmentBillboardPipeline == true ? DrawRecord.mPipeline : nullptr };
				if (Pipeline == nullptr && mIsEnvironmentObjectPipelineInitialized == true) {
					Pipeline = &mEnvironmentObjectPipeline;
				}

				if (Pipeline == nullptr) {
					continue;
				}

				ActivePipeline = Pipeline->Set(ActivePipeline, CommandList);

				DrawRootConstantsB1 RootConstants{};
				RootConstants.FrameGlobalsSrvIndex = FrameGlobalsSrvHandle.GetIndex();
				RootConstants.ModelContextSrvIndex = EnvironmentInstanceContextSrvHandle.GetIndex();
				RootConstants.BonePaletteSrvIndex = EnvironmentSegmentContextSrvHandle.GetIndex();
				RootConstants.DrawRecordSrvIndex = EnvironmentDrawRecordSrvHandle.GetIndex();
				RootConstants.DrawRecordBaseIndex = static_cast<uint32_t>(DrawRecordIndex);
				RootConstants.MaterialSrvIndex = MaterialSrvHandle.GetIndex();
				RootConstants.MaterialTextureTableSrvIndex = MaterialTextureTableSrvHandle.GetIndex();
				RootConstants.ShadowMappingParameterSrvIndex = InvalidDescriptorIndex;
				RootConstants.ShadowMapTextureBaseSrvIndex = InvalidDescriptorIndex;
				RootConstants.FrameGlobalsElementIndex = 0u;
				RootConstants.TerrainPatchContextSrvIndex = InvalidDescriptorIndex;
				RootConstants.Reserved1 = 0u;
				CommandList->SetGraphicsRoot32BitConstants(0, sizeof(DrawRootConstantsB1) / sizeof(uint32_t), &RootConstants, 0);

				CommandList->IASetPrimitiveTopology(Pipeline->GetPrimitiveTopology());

				const std::vector<D3D12_VERTEX_BUFFER_VIEW>& VertexBufferViews{ ResolveVertexBufferViews(*Pipeline, *DrawRecord.mMesh) };
				if (VertexBufferViews.empty() == false) {
					CommandList->IASetVertexBuffers(0, static_cast<UINT>(VertexBufferViews.size()), VertexBufferViews.data());
				}

				const D3D12_INDEX_BUFFER_VIEW& IndexBufferView{ DrawRecord.mMesh->GetIndexBufferView() };
				CommandList->IASetIndexBuffer(&IndexBufferView);

				const Game::ModelSubMesh& SubMesh{ DrawRecord.mMesh->GetSubMesh(DrawRecord.mSubMesh) };
				const UINT IndexCountPerInstance{ static_cast<UINT>(SubMesh.mIndexCount) };
				const UINT InstanceCount{ static_cast<UINT>(DrawRecord.mInstanceCount) };
				const UINT StartIndexLocation{ static_cast<UINT>(SubMesh.mIndexOffset) };
				const INT BaseVertexLocation{ 0 };
				const UINT StartInstanceLocation{ 0 };
				CommandList->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
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

		void DrawCallDispatcher::DrawForwardOverlays(ID3D12GraphicsCommandList* CommandList, const RenderContract::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle BoundingBoxContextSrvHandle, DescriptorHandle DebugGeometryContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle) {
			DrawBoundingBoxes(CommandList, Data, FrameGlobalsSrvHandle, BoundingBoxContextSrvHandle, BonePaletteSrvHandle, DrawRecordSrvHandle, MaterialSrvHandle, MaterialTextureTableSrvHandle);
			DrawDebugGeometries(CommandList, Data, FrameGlobalsSrvHandle, DebugGeometryContextSrvHandle);
		}

		void DrawCallDispatcher::DrawDepthOnly(ID3D12GraphicsCommandList* CommandList, ID3D12GraphicsCommandList9* DynamicDepthBiasCommandList, float RasterDepthBias, float RasterDepthBiasClamp, float RasterSlopeScaledDepthBias, const RenderContract::ShadowRenderContext& ShadowRenderContext, std::uint32_t ShadowFrameGlobalsIndex, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle) {
			const RenderContract::IPipeline* ActivePipeline{ nullptr };
			size_t DrawRecordIndex{ 0 };

			while (DrawRecordIndex < ShadowRenderContext.mDrawRecords.size()) {
				const RenderContract::DrawRecord& StartRecord{ ShadowRenderContext.mDrawRecords[DrawRecordIndex] };
				if (StartRecord.mPipeline == nullptr || StartRecord.mMesh == nullptr || DrawCallDispatcher::IsSkyDomePipeline(StartRecord.mPipeline) == true) {
					DrawRecordIndex += 1;
					continue;
				}

				size_t RunEndIndex{ DrawRecordIndex + 1 };
				while (RunEndIndex < ShadowRenderContext.mDrawRecords.size()) {
					const RenderContract::DrawRecord& NextRecord{ ShadowRenderContext.mDrawRecords[RunEndIndex] };
					bool IsSameRun{ std::tie(NextRecord.mPass, NextRecord.mPipeline, NextRecord.mMesh, NextRecord.mSubMesh) == std::tie(StartRecord.mPass, StartRecord.mPipeline, StartRecord.mMesh, StartRecord.mSubMesh) };
					if (IsSameRun == false) {
						break;
					}
					RunEndIndex += 1;
				}

				const RenderContract::IPipeline* DepthPipeline{ DrawCallDispatcher::ResolveDepthOnlyPipeline(StartRecord) };
				if (DepthPipeline == nullptr) {
					DrawRecordIndex = RunEndIndex;
					continue;
				}

				ActivePipeline = DepthPipeline->Set(ActivePipeline, CommandList);
				if (DynamicDepthBiasCommandList != nullptr) {
					DynamicDepthBiasCommandList->RSSetDepthBias(RasterDepthBias, RasterDepthBiasClamp, RasterSlopeScaledDepthBias);
				}

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

				const std::vector<D3D12_VERTEX_BUFFER_VIEW>& VertexBufferViews{ ResolveVertexBufferViews(*DepthPipeline, *StartRecord.mMesh) };
				if (VertexBufferViews.empty() == false) {
					CommandList->IASetVertexBuffers(0, static_cast<UINT>(VertexBufferViews.size()), VertexBufferViews.data());
				}

				const D3D12_INDEX_BUFFER_VIEW& IndexBufferView{ StartRecord.mMesh->GetIndexBufferView() };
				CommandList->IASetIndexBuffer(&IndexBufferView);

				const Game::ModelSubMesh& SubMesh{ StartRecord.mMesh->GetSubMesh(StartRecord.mSubMesh) };
				UINT IndexCountPerInstance{ static_cast<UINT>(SubMesh.mIndexCount) };
				UINT InstanceCount{ static_cast<UINT>(RunEndIndex - DrawRecordIndex) };
				UINT StartIndexLocation{ static_cast<UINT>(SubMesh.mIndexOffset) };
				INT BaseVertexLocation{ 0 };
				UINT StartInstanceLocation{ 0 };
				CommandList->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
				DrawRecordIndex = RunEndIndex;
			}
		}

		void DrawCallDispatcher::DrawEnvironmentDepthOnly(ID3D12GraphicsCommandList* CommandList, ID3D12GraphicsCommandList9* DynamicDepthBiasCommandList, float RasterDepthBias, float RasterDepthBiasClamp, float RasterSlopeScaledDepthBias, const RenderContract::ShadowRenderContext& ShadowRenderContext, std::uint32_t ShadowFrameGlobalsIndex, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle EnvironmentInstanceContextSrvHandle, DescriptorHandle EnvironmentSegmentContextSrvHandle, DescriptorHandle EnvironmentDrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle) {
			if (CommandList == nullptr || ShadowRenderContext.mEnvironmentDrawRecords.empty() == true) {
				return;
			}

			const RenderContract::IPipeline* ActivePipeline{ nullptr };
			for (std::size_t DrawRecordIndex{}; DrawRecordIndex < ShadowRenderContext.mEnvironmentDrawRecords.size(); DrawRecordIndex += 1ULL) {
				const RenderContract::EnvironmentDrawRecord& DrawRecord{ ShadowRenderContext.mEnvironmentDrawRecords[DrawRecordIndex] };
				if (DrawRecord.mMesh == nullptr || DrawRecord.mInstanceCount == 0u || DrawRecord.mCastsShadow == false) {
					continue;
				}

				const RenderContract::IPipeline* DepthPipeline{ DrawCallDispatcher::ResolveEnvironmentDepthOnlyPipeline(DrawRecord) };
				if (DepthPipeline == nullptr) {
					continue;
				}

				ActivePipeline = DepthPipeline->Set(ActivePipeline, CommandList);
				if (DynamicDepthBiasCommandList != nullptr) {
					DynamicDepthBiasCommandList->RSSetDepthBias(RasterDepthBias, RasterDepthBiasClamp, RasterSlopeScaledDepthBias);
				}

				DrawRootConstantsB1 RootConstants{};
				RootConstants.FrameGlobalsSrvIndex = FrameGlobalsSrvHandle.GetIndex();
				RootConstants.ModelContextSrvIndex = EnvironmentInstanceContextSrvHandle.GetIndex();
				RootConstants.BonePaletteSrvIndex = EnvironmentSegmentContextSrvHandle.GetIndex();
				RootConstants.DrawRecordSrvIndex = EnvironmentDrawRecordSrvHandle.GetIndex();
				RootConstants.DrawRecordBaseIndex = static_cast<uint32_t>(DrawRecordIndex);
				RootConstants.MaterialSrvIndex = MaterialSrvHandle.GetIndex();
				RootConstants.MaterialTextureTableSrvIndex = MaterialTextureTableSrvHandle.GetIndex();
				RootConstants.ShadowMappingParameterSrvIndex = InvalidDescriptorIndex;
				RootConstants.ShadowMapTextureBaseSrvIndex = InvalidDescriptorIndex;
				RootConstants.FrameGlobalsElementIndex = ShadowFrameGlobalsIndex;
				RootConstants.TerrainPatchContextSrvIndex = InvalidDescriptorIndex;
				RootConstants.Reserved1 = 0u;
				CommandList->SetGraphicsRoot32BitConstants(0, sizeof(DrawRootConstantsB1) / sizeof(uint32_t), &RootConstants, 0);

				CommandList->IASetPrimitiveTopology(DepthPipeline->GetPrimitiveTopology());

				const std::vector<D3D12_VERTEX_BUFFER_VIEW>& VertexBufferViews{ ResolveVertexBufferViews(*DepthPipeline, *DrawRecord.mMesh) };
				if (VertexBufferViews.empty() == false) {
					CommandList->IASetVertexBuffers(0, static_cast<UINT>(VertexBufferViews.size()), VertexBufferViews.data());
				}

				const D3D12_INDEX_BUFFER_VIEW& IndexBufferView{ DrawRecord.mMesh->GetIndexBufferView() };
				CommandList->IASetIndexBuffer(&IndexBufferView);

				const Game::ModelSubMesh& SubMesh{ DrawRecord.mMesh->GetSubMesh(DrawRecord.mSubMesh) };
				const UINT IndexCountPerInstance{ static_cast<UINT>(SubMesh.mIndexCount) };
				const UINT InstanceCount{ static_cast<UINT>(DrawRecord.mInstanceCount) };
				const UINT StartIndexLocation{ static_cast<UINT>(SubMesh.mIndexOffset) };
				const INT BaseVertexLocation{ 0 };
				const UINT StartInstanceLocation{ 0 };
				CommandList->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
			}
		}

		const std::vector<D3D12_VERTEX_BUFFER_VIEW>& DrawCallDispatcher::ResolveVertexBufferViews(const RenderContract::IPipeline& Pipeline, const RenderContract::IModelNode& Mesh) {
			const std::pair<const RenderContract::IPipeline*, const RenderContract::IModelNode*> CacheKey{ &Pipeline, &Mesh };
			std::map<std::pair<const RenderContract::IPipeline*, const RenderContract::IModelNode*>, std::vector<D3D12_VERTEX_BUFFER_VIEW>>::iterator CacheIter{ mVertexBufferViewCache.find(CacheKey) };
			if (CacheIter != mVertexBufferViewCache.end()) {
				return CacheIter->second;
			}

			std::vector<D3D12_VERTEX_BUFFER_VIEW> VertexBufferViews{ BuildVertexBufferViews(Pipeline, Mesh) };
			std::pair<std::map<std::pair<const RenderContract::IPipeline*, const RenderContract::IModelNode*>, std::vector<D3D12_VERTEX_BUFFER_VIEW>>::iterator, bool> InsertResult{ mVertexBufferViewCache.emplace(CacheKey, std::move(VertexBufferViews)) };
			return InsertResult.first->second;
		}

		bool DrawCallDispatcher::IsSkyDomePipeline(const RenderContract::IPipeline* Pipeline) {
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

		const RenderContract::IPipeline* DrawCallDispatcher::ResolveDepthOnlyPipeline(const RenderContract::DrawRecord& DrawRecord) {
			if (DrawRecord.mPipeline == nullptr) {
				return nullptr;
			}

			if (DrawRecord.mTerrainPatchContextIndex != InvalidDescriptorIndex) {
				if (mIsTerrainDepthPipelineInitialized == false) {
					mIsTerrainDepthPipelineInitialized = mTerrainDepthPipeline.Initialize("TerrainDepthGraphics");
				}

				return mIsTerrainDepthPipelineInitialized == true ? &mTerrainDepthPipeline : nullptr;
			}

			const bool HasSkinningInput{ HasVertexInputBinding(*DrawRecord.mPipeline, Game::VertexAttributeKind::BoneIndices) || HasVertexInputBinding(*DrawRecord.mPipeline, Game::VertexAttributeKind::BoneWeights) };
			if (DrawRecord.mPipeline->HasOption(Game::PipelineOption::DepthAlphaCutoff) == true && HasSkinningInput == false) {
				if (mIsDepthAlphaCutoffPipelineInitialized == false) {
					mIsDepthAlphaCutoffPipelineInitialized = mDepthAlphaCutoffPipeline.Initialize("AlphaCutoffDepthGraphics");
				}

				return mIsDepthAlphaCutoffPipelineInitialized == true ? &mDepthAlphaCutoffPipeline : nullptr;
			}

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

		const RenderContract::IPipeline* DrawCallDispatcher::ResolveEnvironmentDepthOnlyPipeline(const RenderContract::EnvironmentDrawRecord& DrawRecord) {
			if (DrawRecord.mPipeline != nullptr && DrawRecord.mPipeline->GetPrimitiveTopology() == D3D_PRIMITIVE_TOPOLOGY_POINTLIST) {
				if (mIsEnvironmentBillboardDepthPipelineInitialized == false) {
					mIsEnvironmentBillboardDepthPipelineInitialized = mEnvironmentBillboardDepthPipeline.Initialize("EnvironmentBillboardDepthGraphics");
				}

				return mIsEnvironmentBillboardDepthPipelineInitialized == true ? &mEnvironmentBillboardDepthPipeline : nullptr;
			}

			if (mIsEnvironmentObjectDepthPipelineInitialized == false) {
				mIsEnvironmentObjectDepthPipelineInitialized = mEnvironmentObjectDepthPipeline.Initialize("EnvironmentObjectDepthGraphics");
			}

			return mIsEnvironmentObjectDepthPipelineInitialized == true ? &mEnvironmentObjectDepthPipeline : nullptr;
		}

		void DrawCallDispatcher::DrawSkyDome(ID3D12GraphicsCommandList* CommandList, const RenderContract::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle TerrainPatchContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle) {
			if (CommandList == nullptr) {
				return;
			}

			if (mIsSkyDomePipelineInitialized == false) {
				mIsSkyDomePipelineInitialized = mSkyDomePipeline.Initialize("SkyDomeGraphics");
			}

			if (mIsSkyDomePipelineInitialized == false) {
				return;
			}

			const RenderContract::IPipeline* ActivePipeline{ nullptr };
			size_t DrawRecordIndex{ 0 };

			while (DrawRecordIndex < Data.mDrawRecords.size()) {
				const RenderContract::DrawRecord& StartRecord{ Data.mDrawRecords[DrawRecordIndex] };
				if (StartRecord.mPipeline == nullptr || StartRecord.mMesh == nullptr || DrawCallDispatcher::IsSkyDomePipeline(StartRecord.mPipeline) == false) {
					DrawRecordIndex += 1;
					continue;
				}

				size_t RunEndIndex{ DrawRecordIndex + 1 };
				while (RunEndIndex < Data.mDrawRecords.size()) {
					const RenderContract::DrawRecord& NextRecord{ Data.mDrawRecords[RunEndIndex] };
					bool IsSameRun{ std::tie(NextRecord.mPass, NextRecord.mPipeline, NextRecord.mMesh, NextRecord.mSubMesh) == std::tie(StartRecord.mPass, StartRecord.mPipeline, StartRecord.mMesh, StartRecord.mSubMesh) };
					if (IsSameRun == false) {
						break;
					}
					RunEndIndex += 1;
				}

				ActivePipeline = StartRecord.mPipeline->Set(ActivePipeline, CommandList);

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

				CommandList->IASetPrimitiveTopology(StartRecord.mPipeline->GetPrimitiveTopology());

				const std::vector<D3D12_VERTEX_BUFFER_VIEW>& VertexBufferViews{ ResolveVertexBufferViews(*StartRecord.mPipeline, *StartRecord.mMesh) };
				if (VertexBufferViews.empty() == false) {
					CommandList->IASetVertexBuffers(0, static_cast<UINT>(VertexBufferViews.size()), VertexBufferViews.data());
				}

				const D3D12_INDEX_BUFFER_VIEW& IndexBufferView{ StartRecord.mMesh->GetIndexBufferView() };
				CommandList->IASetIndexBuffer(&IndexBufferView);

				const Game::ModelSubMesh& SubMesh{ StartRecord.mMesh->GetSubMesh(StartRecord.mSubMesh) };
				UINT IndexCountPerInstance{ static_cast<UINT>(SubMesh.mIndexCount) };
				UINT InstanceCount{ static_cast<UINT>(RunEndIndex - DrawRecordIndex) };
				UINT StartIndexLocation{ static_cast<UINT>(SubMesh.mIndexOffset) };
				INT BaseVertexLocation{ 0 };
				UINT StartInstanceLocation{ 0 };
				CommandList->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
				DrawRecordIndex = RunEndIndex;
			}
		}

		void DrawCallDispatcher::DrawBoundingBoxes(ID3D12GraphicsCommandList* CommandList, const RenderContract::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle BoundingBoxContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle) {
			if (CommandList == nullptr) {
				return;
			}

			const bool IsDrawBoundingBoxesEnabled{ (Data.mFrameGlobals.mFlags & RenderContract::FrameGlobalFlagDrawBoundingBoxes) != 0u };
			if (IsDrawBoundingBoxesEnabled == false || Data.mBoundingBoxContexts.empty()) {
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
			CommandList->DrawInstanced(1u, static_cast<UINT>(Data.mBoundingBoxContexts.size()), 0u, 0u);
		}

		void DrawCallDispatcher::DrawDebugGeometries(ID3D12GraphicsCommandList* CommandList, const RenderContract::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle DebugGeometryContextSrvHandle) {
			if (CommandList == nullptr) {
				return;
			}

			const bool IsDrawDebugGeometriesEnabled{ (Data.mFrameGlobals.mFlags & RenderContract::FrameGlobalFlagDrawDebugGeometry) != 0u };
			if (IsDrawDebugGeometriesEnabled == false || Data.mDebugGeometryContexts.empty()) {
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
			CommandList->DrawInstanced(1u, static_cast<UINT>(Data.mDebugGeometryContexts.size()), 0u, 0u);
		}
	}
}
