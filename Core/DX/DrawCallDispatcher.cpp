#include "DrawCallDispatcher.h"

namespace Core {
	namespace DX {
		namespace {
			struct DrawRootConstantsB1 {
				uint32_t FrameGlobalsSrvIndex{ 0 };
				uint32_t ModelContextSrvIndex{ 0 };
				uint32_t DrawRecordSrvIndex{ 0 };
				uint32_t DrawRecordBaseIndex{ 0 };
				uint32_t MaterialSrvIndex{ 0 };
				uint32_t MaterialTextureTableSrvIndex{ 0 };
				float TintColor[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
			};
		}

		DrawCallDispatcher::DrawCallDispatcher() {
		}

		DrawCallDispatcher::~DrawCallDispatcher() {
		}

		void DrawCallDispatcher::DrawForward(ID3D12GraphicsCommandList* CommandList, Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle DrawRecordSrvHandle, DescriptorHandle MaterialSrvHandle, DescriptorHandle MaterialTextureTableSrvHandle) {
			const Interface::IPipeline* ActivePipeline = nullptr;
			size_t DrawRecordIndex = 0;

			while (DrawRecordIndex < Data.drawRecords.size()) {
				Game::RFD::DrawRecord& StartRecord = Data.drawRecords[DrawRecordIndex];
				if (StartRecord.pso == nullptr || StartRecord.mesh == nullptr) {
					DrawRecordIndex += 1;
					continue;
				}

				size_t RunEndIndex = DrawRecordIndex + 1;
				while (RunEndIndex < Data.drawRecords.size()) {
					const Game::RFD::DrawRecord& NextRecord = Data.drawRecords[RunEndIndex];
					bool IsSameRun = NextRecord.pass == StartRecord.pass && NextRecord.pso == StartRecord.pso && NextRecord.mesh == StartRecord.mesh && NextRecord.submesh == StartRecord.submesh;
					if (IsSameRun == false) {
						break;
					}
					RunEndIndex += 1;
				}

				ActivePipeline = StartRecord.pso->Set(ActivePipeline, CommandList);

				DrawRootConstantsB1 RootConstants{};
				RootConstants.FrameGlobalsSrvIndex = FrameGlobalsSrvHandle.GetIndex();
				RootConstants.ModelContextSrvIndex = ModelContextSrvHandle.GetIndex();
				RootConstants.DrawRecordSrvIndex = DrawRecordSrvHandle.GetIndex();
				RootConstants.DrawRecordBaseIndex = static_cast<uint32_t>(DrawRecordIndex);
				RootConstants.MaterialSrvIndex = MaterialSrvHandle.GetIndex();
				RootConstants.MaterialTextureTableSrvIndex = MaterialTextureTableSrvHandle.GetIndex();
				CommandList->SetGraphicsRoot32BitConstants(0, sizeof(DrawRootConstantsB1) / sizeof(uint32_t), &RootConstants, 0);

				CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				const std::vector<D3D12_VERTEX_BUFFER_VIEW>& VertexBufferViews = StartRecord.mesh->GetVertexBufferViews();
				if (VertexBufferViews.empty() == false) {
					CommandList->IASetVertexBuffers(0, static_cast<UINT>(VertexBufferViews.size()), VertexBufferViews.data());
				}

				const D3D12_INDEX_BUFFER_VIEW& IndexBufferView = StartRecord.mesh->GetIndexBufferView();
				CommandList->IASetIndexBuffer(&IndexBufferView);

				const Game::ModelSubMesh& SubMesh = StartRecord.mesh->GetSubMesh(StartRecord.submesh);
				UINT IndexCountPerInstance = static_cast<UINT>(SubMesh.IndexCount);
				UINT InstanceCount = static_cast<UINT>(RunEndIndex - DrawRecordIndex);
				UINT StartIndexLocation = static_cast<UINT>(SubMesh.IndexOffset);
				INT BaseVertexLocation = 0;
				UINT StartInstanceLocation = 0;
				CommandList->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
				DrawRecordIndex = RunEndIndex;
			}
		}
	}
}
