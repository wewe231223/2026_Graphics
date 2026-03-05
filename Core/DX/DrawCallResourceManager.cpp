#include "DrawCallResourceManager.h"
#include <algorithm>
#include <array>
#include "Core/DX/CopyQueueId.h"
#include "Core/DX/GraphicsAllocator.h"
#include "Utility/ErrorHandler.h"

namespace Core {
	namespace DX {
		DrawCallResourceManager::DrawCallResourceManager() {
		}

		DrawCallResourceManager::~DrawCallResourceManager() {
		}

		void DrawCallResourceManager::Initialize(ID3D12Device* Device, DescriptorHeap* SrvHeap, std::uint32_t FrameIndex) {
			mDevice = Device;
			mSrvHeap = SrvHeap;
			mFrameGlobalsSrvHandle = mSrvHeap->Allocate();
			mModelContextSrvHandle = mSrvHeap->Allocate();
			mDrawRecordSrvHandle = mSrvHeap->Allocate();
			mCopyFenceValue = 0;
			mCopyId = CopyQueueId::DrawCallBegin + static_cast<std::uint64_t>(FrameIndex);
			bool IsCopyIdValid{ mCopyId <= CopyQueueId::DrawCallEnd };
			ErrorHandler::report(IsCopyIdValid == false, "DrawCallResourceManager", "Invalid draw call copy id.", ErrorHandler::Level::Critical);
		}

		void DrawCallResourceManager::PrepareFrameResources(Game::RFD::RenderFrameData& Data, GraphicsAllocator& GraphicsAllocator, Interface::ICopyQueue* CopyQueue) {
			std::stable_sort(Data.drawRecords.begin(), Data.drawRecords.end(), DrawCallResourceManager::CompareDrawRecordByPso);
			DrawCallResourceManager::BuildDrawRecordGpu(Data);

			std::size_t FrameGlobalsSizeInBytes{ sizeof(Game::RFD::FrameGlobals) };
			std::size_t ModelContextsSizeInBytes{ sizeof(Game::RFD::ModelContext) * Data.modelContexts.size() };
			std::size_t DrawRecordsGpuSizeInBytes{ sizeof(DrawRecordGPU) * mDrawRecordsGpu.size() };

			std::byte DummyByte{ 0 };
			void* FrameGlobalsSourceData{ FrameGlobalsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(&Data.globals) };
			void* ModelContextSourceData{ ModelContextsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(Data.modelContexts.data()) };
			void* DrawRecordSourceData{ DrawRecordsGpuSizeInBytes == 0 ? &DummyByte : static_cast<void*>(mDrawRecordsGpu.data()) };

			bool FrameGlobalsCopyResult{ mFrameGlobalsVector.Copy(GraphicsAllocator, FrameGlobalsSourceData, FrameGlobalsSizeInBytes) };
			ErrorHandler::report(FrameGlobalsCopyResult == false, "DrawCallResourceManager", "Failed to copy frame globals data.", ErrorHandler::Level::Critical);

			bool ModelContextCopyResult{ mModelContextVector.Copy(GraphicsAllocator, ModelContextSourceData, ModelContextsSizeInBytes) };
			ErrorHandler::report(ModelContextCopyResult == false, "DrawCallResourceManager", "Failed to copy model context data.", ErrorHandler::Level::Critical);

			bool DrawRecordCopyResult{ mDrawRecordVector.Copy(GraphicsAllocator, DrawRecordSourceData, DrawRecordsGpuSizeInBytes) };
			ErrorHandler::report(DrawRecordCopyResult == false, "DrawCallResourceManager", "Failed to copy draw record data.", ErrorHandler::Level::Critical);

			std::array<Interface::CopyQueueCopyRequest, 3> CopyRequests{ mFrameGlobalsVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0), mModelContextVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0), mDrawRecordVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0) };
			bool EnqueueResult{ CopyQueue->EnqueueCopy(mCopyId, CopyRequests) };
			ErrorHandler::report(EnqueueResult == false, "DrawCallResourceManager", "Failed to enqueue frame upload copy requests.", ErrorHandler::Level::Critical);
			mCopyFenceValue = mCopyId;

			DrawCallResourceManager::UpdateShaderResourceViews(1, static_cast<std::uint32_t>(Data.modelContexts.size()), static_cast<std::uint32_t>(mDrawRecordsGpu.size()));
		}

		void DrawCallResourceManager::TransitionToShaderResource(ID3D12GraphicsCommandList* CommandList) {
			if (mFrameGlobalsVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER FrameGlobalsBarrier{ mFrameGlobalsVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) };
				CommandList->ResourceBarrier(1, &FrameGlobalsBarrier);
			}

			if (mModelContextVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER ModelContextBarrier{ mModelContextVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) };
				CommandList->ResourceBarrier(1, &ModelContextBarrier);
			}

			if (mDrawRecordVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER DrawRecordBarrier{ mDrawRecordVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) };
				CommandList->ResourceBarrier(1, &DrawRecordBarrier);
			}
		}

		void DrawCallResourceManager::TransitionToCopyDestination(ID3D12GraphicsCommandList* CommandList) {
			if (mFrameGlobalsVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER FrameGlobalsBarrier{ mFrameGlobalsVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST) };
				CommandList->ResourceBarrier(1, &FrameGlobalsBarrier);
			}

			if (mModelContextVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER ModelContextBarrier{ mModelContextVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST) };
				CommandList->ResourceBarrier(1, &ModelContextBarrier);
			}

			if (mDrawRecordVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER DrawRecordBarrier{ mDrawRecordVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST) };
				CommandList->ResourceBarrier(1, &DrawRecordBarrier);
			}
		}

		void DrawCallResourceManager::WaitForUpload(Interface::ICopyQueue* CopyQueue) const {
			if (mCopyFenceValue == 0) {
				return;
			}

			CopyQueue->GuaranteeCopy(mCopyFenceValue);
		}

		DescriptorHandle DrawCallResourceManager::GetFrameGlobalsSrvHandle() const {
			return mFrameGlobalsSrvHandle;
		}

		DescriptorHandle DrawCallResourceManager::GetModelContextSrvHandle() const {
			return mModelContextSrvHandle;
		}

		DescriptorHandle DrawCallResourceManager::GetDrawRecordSrvHandle() const {
			return mDrawRecordSrvHandle;
		}

		bool DrawCallResourceManager::CompareDrawRecordByPso(const Game::RFD::DrawRecord& Left, const Game::RFD::DrawRecord& Right) {
			if (Left.pass != Right.pass) {
				return Left.pass < Right.pass;
			}

			if (Left.pso != Right.pso) {
				return Left.pso < Right.pso;
			}

			if (Left.mesh != Right.mesh) {
				return Left.mesh < Right.mesh;
			}

			if (Left.submesh != Right.submesh) {
				return Left.submesh < Right.submesh;
			}

			return false;
		}

		void DrawCallResourceManager::BuildDrawRecordGpu(const Game::RFD::RenderFrameData& Data) {
			mDrawRecordsGpu.resize(Data.drawRecords.size());
			for (std::size_t Index{ 0 }; Index < Data.drawRecords.size(); ++Index) {
				const Game::RFD::DrawRecord& SourceRecord{ Data.drawRecords[Index] };
				DrawRecordGPU& DestinationRecord{ mDrawRecordsGpu[Index] };
				DestinationRecord.ObjectIndex = SourceRecord.objectIndex;
				DestinationRecord.MaterialIndex = SourceRecord.materialIndex;
				DestinationRecord.Flags = SourceRecord.flags;
				DestinationRecord.Pad0 = SourceRecord.pad0;
			}
		}

		void DrawCallResourceManager::UpdateShaderResourceViews(std::uint32_t FrameGlobalsCount, std::uint32_t ModelContextCount, std::uint32_t DrawRecordCount) {
			ID3D12Resource* FrameGlobalsResource{ mFrameGlobalsVector.IsValid() == true ? mFrameGlobalsVector.GetResource() : nullptr };
			ID3D12Resource* ModelContextResource{ mModelContextVector.IsValid() == true ? mModelContextVector.GetResource() : nullptr };
			ID3D12Resource* DrawRecordResource{ mDrawRecordVector.IsValid() == true ? mDrawRecordVector.GetResource() : nullptr };

			if (mFrameGlobalsVector.IsValid() == true) {
				bool IsUpdateRequired{ DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mFrameGlobalsSrvResource, FrameGlobalsResource, mFrameGlobalsSrvElementCount, FrameGlobalsCount) };
				if (IsUpdateRequired == true) {
					mFrameGlobalsVector.CreateShaderResourceView(mDevice, mFrameGlobalsSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, FrameGlobalsCount, sizeof(Game::RFD::FrameGlobals), D3D12_BUFFER_SRV_FLAG_NONE);
					mFrameGlobalsSrvResource = FrameGlobalsResource;
					mFrameGlobalsSrvElementCount = FrameGlobalsCount;
				}
			}
			else {
				mFrameGlobalsSrvResource = nullptr;
				mFrameGlobalsSrvElementCount = 0;
			}

			if (mModelContextVector.IsValid() == true) {
				bool IsUpdateRequired{ DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mModelContextSrvResource, ModelContextResource, mModelContextSrvElementCount, ModelContextCount) };
				if (IsUpdateRequired == true) {
					mModelContextVector.CreateShaderResourceView(mDevice, mModelContextSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, ModelContextCount, sizeof(Game::RFD::ModelContext), D3D12_BUFFER_SRV_FLAG_NONE);
					mModelContextSrvResource = ModelContextResource;
					mModelContextSrvElementCount = ModelContextCount;
				}
			}
			else {
				mModelContextSrvResource = nullptr;
				mModelContextSrvElementCount = 0;
			}

			if (mDrawRecordVector.IsValid() == true) {
				bool IsUpdateRequired{ DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mDrawRecordSrvResource, DrawRecordResource, mDrawRecordSrvElementCount, DrawRecordCount) };
				if (IsUpdateRequired == true) {
					mDrawRecordVector.CreateShaderResourceView(mDevice, mDrawRecordSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, DrawRecordCount, sizeof(DrawRecordGPU), D3D12_BUFFER_SRV_FLAG_NONE);
					mDrawRecordSrvResource = DrawRecordResource;
					mDrawRecordSrvElementCount = DrawRecordCount;
				}
			}
			else {
				mDrawRecordSrvResource = nullptr;
				mDrawRecordSrvElementCount = 0;
			}
		}

		bool DrawCallResourceManager::IsShaderResourceViewUpdateRequired(ID3D12Resource* CachedResource, ID3D12Resource* CurrentResource, std::uint32_t CachedElementCount, std::uint32_t CurrentElementCount) const {
			if (CurrentResource == nullptr) {
				return false;
			}

			if (CachedResource != CurrentResource) {
				return true;
			}

			if (CachedElementCount != CurrentElementCount) {
				return true;
			}

			return false;
		}
	}
}
