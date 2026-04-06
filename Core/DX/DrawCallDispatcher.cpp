#include "DrawCallDispatcher.h"
#include <vector>

namespace Core {
	namespace DX {
		namespace {
			struct DrawRootConstantsB1 {
				uint32_t FrameGlobalsSrvIndex{ 0 };
				uint32_t ModelContextSrvIndex{ 0 };
				uint32_t BonePaletteSrvIndex{ 0 };
				uint32_t DrawRecordSrvIndex{ 0 };
				uint32_t DrawRecordBaseIndex{ 0 };
				uint32_t MaterialSrvIndex{ 0 };
				uint32_t MaterialTextureTableSrvIndex{ 0 };
				float TintColor[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
			};

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
			: mBoundingBoxLinePipeline{},
			mIsBoundingBoxLinePipelineInitialized{} {
		}

		DrawCallDispatcher::~DrawCallDispatcher() {
		}

		void DrawCallDispatcher::DrawForward(ID3D12GraphicsCommandList* CommandList, Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle) {
			const Interface::IPipeline* ActivePipeline{ nullptr };
			size_t DrawRecordIndex{ 0 };

			while (DrawRecordIndex < Data.drawRecords.size()) {
				Game::RFD::DrawRecord& StartRecord{ Data.drawRecords[DrawRecordIndex] };
				if (StartRecord.pso == nullptr || StartRecord.mesh == nullptr) {
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
				CommandList->SetGraphicsRoot32BitConstants(0, sizeof(DrawRootConstantsB1) / sizeof(uint32_t), &RootConstants, 0);

				CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

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

			DrawBoundingBoxes(CommandList, Data, FrameGlobalsSrvHandle, ModelContextSrvHandle, BonePaletteSrvHandle, DrawRecordSrvHandle, MaterialSrvHandle, MaterialTextureTableSrvHandle);
		}

		void DrawCallDispatcher::DrawBoundingBoxes(ID3D12GraphicsCommandList* CommandList, const Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle BonePaletteSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle) {
			if (CommandList == nullptr) {
				return;
			}

			const bool IsDrawBoundingBoxesEnabled{ (Data.globals.flags & Game::RFD::FrameGlobalFlagDrawBoundingBoxes) != 0u };
			if (IsDrawBoundingBoxesEnabled == false || Data.modelContexts.empty()) {
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
			RootConstants.ModelContextSrvIndex = ModelContextSrvHandle.GetIndex();
			RootConstants.BonePaletteSrvIndex = BonePaletteSrvHandle.GetIndex();
			RootConstants.DrawRecordSrvIndex = DrawRecordSrvHandle.GetIndex();
			RootConstants.DrawRecordBaseIndex = 0u;
			RootConstants.MaterialSrvIndex = MaterialSrvHandle.GetIndex();
			RootConstants.MaterialTextureTableSrvIndex = MaterialTextureTableSrvHandle.GetIndex();
			CommandList->SetGraphicsRoot32BitConstants(0, sizeof(DrawRootConstantsB1) / sizeof(uint32_t), &RootConstants, 0);

			CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
			CommandList->DrawInstanced(1u, static_cast<UINT>(Data.modelContexts.size()), 0u, 0u);
		}
	}
}
