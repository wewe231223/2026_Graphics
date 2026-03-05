#include "DrawCallResourceManager.h"
#include <algorithm>
#include "Core/DX/GraphicsAllocator.h"
#include "Core/DX/CopyQueueId.h"
#include "Utility/ErrorHandler.h"

namespace Core {
	namespace DX {
		DrawCallResourceManager::DrawCallResourceManager() {
		}

		DrawCallResourceManager::~DrawCallResourceManager() {
		}

		void DrawCallResourceManager::Initialize(ID3D12Device* Device, DescriptorHeap* SrvHeap) {
			mDevice = Device;
			mSrvHeap = SrvHeap;

			for (size_t Index = 0; Index < Constants::FrameCount<size_t>; ++Index) {
				mFrameGlobalsSrvHandles[Index] = mSrvHeap->Allocate();
				mModelContextSrvHandles[Index] = mSrvHeap->Allocate();
				mDrawRecordSrvHandles[Index] = mSrvHeap->Allocate();
				mMaterialSrvHandles[Index] = mSrvHeap->Allocate();
				mMaterialTextureTableSrvHandles[Index] = mSrvHeap->Allocate();
				mPerFrameCopyFenceValues[Index] = 0;
			}

			DescriptorHandle ReservedHandle{};
			while (true) {
				ReservedHandle = mSrvHeap->Allocate();
				if (ReservedHandle.GetIndex() >= 29) {
					break;
				}
			}
		}

		void DrawCallResourceManager::PrepareFrameResources(uint32_t RtvIndex, Game::RFD::RenderFrameData& Data, GraphicsAllocator& GraphicsAllocator, Interface::ICopyQueue& CopyQueue) {
			std::stable_sort(Data.drawRecords.begin(), Data.drawRecords.end(), DrawCallResourceManager::CompareDrawRecordByPso);
			DrawCallResourceManager::BuildDrawRecordGpu(Data);

			GraphicsVector& FrameGlobalsVector = mPerFrameFrameGlobalsVectors[RtvIndex];
			GraphicsVector& ModelContextVector = mPerFrameModelContextVectors[RtvIndex];
			GraphicsVector& DrawRecordVector = mPerFrameDrawRecordVectors[RtvIndex];
			GraphicsVector& MaterialVector = mPerFrameMaterialVectors[RtvIndex];
			GraphicsVector& MaterialTextureTableVector = mPerFrameMaterialTextureTableVectors[RtvIndex];

			size_t FrameGlobalsSizeInBytes = sizeof(Game::RFD::FrameGlobals);
			size_t ModelContextsSizeInBytes = sizeof(Game::RFD::ModelContext) * Data.modelContexts.size();
			size_t DrawRecordsGpuSizeInBytes = sizeof(DrawRecordGPU) * mDrawRecordsGpu.size();
			size_t MaterialsSizeInBytes = sizeof(Game::RFD::MaterialGpu) * Data.materials.size();
			size_t MaterialTextureTableSizeInBytes = sizeof(Game::RFD::MaterialTextureTableItemGpu) * Data.materialTextureTable.size();

			std::byte DummyByte{ 0 };
			void* FrameGlobalsSourceData = FrameGlobalsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(&Data.globals);
			void* ModelContextSourceData = ModelContextsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(Data.modelContexts.data());
			void* DrawRecordSourceData = DrawRecordsGpuSizeInBytes == 0 ? &DummyByte : static_cast<void*>(mDrawRecordsGpu.data());
			void* MaterialSourceData = MaterialsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(Data.materials.data());
			void* MaterialTextureTableSourceData = MaterialTextureTableSizeInBytes == 0 ? &DummyByte : static_cast<void*>(Data.materialTextureTable.data());

			bool FrameGlobalsCopyResult = FrameGlobalsVector.Copy(GraphicsAllocator, FrameGlobalsSourceData, FrameGlobalsSizeInBytes);
			ErrorHandler::report(FrameGlobalsCopyResult == false, "DrawCallResourceManager", "Failed to copy frame globals data.", ErrorHandler::Level::Critical);

			bool ModelContextCopyResult = ModelContextVector.Copy(GraphicsAllocator, ModelContextSourceData, ModelContextsSizeInBytes);
			ErrorHandler::report(ModelContextCopyResult == false, "DrawCallResourceManager", "Failed to copy model context data.", ErrorHandler::Level::Critical);

			bool DrawRecordCopyResult = DrawRecordVector.Copy(GraphicsAllocator, DrawRecordSourceData, DrawRecordsGpuSizeInBytes);
			ErrorHandler::report(DrawRecordCopyResult == false, "DrawCallResourceManager", "Failed to copy draw record data.", ErrorHandler::Level::Critical);

			bool MaterialCopyResult = MaterialVector.Copy(GraphicsAllocator, MaterialSourceData, MaterialsSizeInBytes);
			ErrorHandler::report(MaterialCopyResult == false, "DrawCallResourceManager", "Failed to copy material data.", ErrorHandler::Level::Critical);

			bool MaterialTextureTableCopyResult = MaterialTextureTableVector.Copy(GraphicsAllocator, MaterialTextureTableSourceData, MaterialTextureTableSizeInBytes);
			ErrorHandler::report(MaterialTextureTableCopyResult == false, "DrawCallResourceManager", "Failed to copy material texture table data.", ErrorHandler::Level::Critical);

			std::array<Interface::CopyQueueCopyRequest, 5> CopyRequests{ 
				FrameGlobalsVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0), 
				ModelContextVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0), 
				DrawRecordVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0), 
				MaterialVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0), 
				MaterialTextureTableVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0) 
			};

			uint64_t CopyId = CopyQueueId::DrawCallBegin + static_cast<uint64_t>(RtvIndex);
			bool IsCopyIdValid = CopyId <= CopyQueueId::DrawCallEnd;
			ErrorHandler::report(IsCopyIdValid == false, "DrawCallResourceManager", "Invalid draw call copy id.", ErrorHandler::Level::Critical);
			bool EnqueueResult = CopyQueue.EnqueueCopy(CopyId, CopyRequests);
			ErrorHandler::report(EnqueueResult == false, "DrawCallResourceManager", "Failed to enqueue frame upload copy requests.", ErrorHandler::Level::Critical);
			CopyQueue.DispatchCopies();
			mPerFrameCopyFenceValues[RtvIndex] = CopyId;

			DrawCallResourceManager::UpdateShaderResourceViews(RtvIndex, 1, static_cast<uint32_t>(Data.modelContexts.size()), static_cast<uint32_t>(mDrawRecordsGpu.size()), static_cast<uint32_t>(Data.materials.size()), static_cast<uint32_t>(Data.materialTextureTable.size()));
		}

		void DrawCallResourceManager::TransitionToShaderResource(ID3D12GraphicsCommandList* CommandList, uint32_t RtvIndex) {
			GraphicsVector& FrameGlobalsVector = mPerFrameFrameGlobalsVectors[RtvIndex];
			GraphicsVector& ModelContextVector = mPerFrameModelContextVectors[RtvIndex];
			GraphicsVector& DrawRecordVector = mPerFrameDrawRecordVectors[RtvIndex];
			GraphicsVector& MaterialVector = mPerFrameMaterialVectors[RtvIndex];
			GraphicsVector& MaterialTextureTableVector = mPerFrameMaterialTextureTableVectors[RtvIndex];

			if (FrameGlobalsVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER FrameGlobalsBarrier = FrameGlobalsVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
				CommandList->ResourceBarrier(1, &FrameGlobalsBarrier);
			}

			if (ModelContextVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER ModelContextBarrier = ModelContextVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
				CommandList->ResourceBarrier(1, &ModelContextBarrier);
			}

			if (DrawRecordVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER DrawRecordBarrier = DrawRecordVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
				CommandList->ResourceBarrier(1, &DrawRecordBarrier);
			}

			if (MaterialVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER MaterialBarrier = MaterialVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
				CommandList->ResourceBarrier(1, &MaterialBarrier);
			}

			if (MaterialTextureTableVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER MaterialTextureTableBarrier = MaterialTextureTableVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
				CommandList->ResourceBarrier(1, &MaterialTextureTableBarrier);
			}
		}

		void DrawCallResourceManager::TransitionToCopyDestination(ID3D12GraphicsCommandList* CommandList, uint32_t RtvIndex) {
			GraphicsVector& FrameGlobalsVector = mPerFrameFrameGlobalsVectors[RtvIndex];
			GraphicsVector& ModelContextVector = mPerFrameModelContextVectors[RtvIndex];
			GraphicsVector& DrawRecordVector = mPerFrameDrawRecordVectors[RtvIndex];
			GraphicsVector& MaterialVector = mPerFrameMaterialVectors[RtvIndex];
			GraphicsVector& MaterialTextureTableVector = mPerFrameMaterialTextureTableVectors[RtvIndex];

			if (ModelContextVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER ModelContextBarrier = ModelContextVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST);
				CommandList->ResourceBarrier(1, &ModelContextBarrier);
			}

			if (DrawRecordVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER DrawRecordBarrier = DrawRecordVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST);
				CommandList->ResourceBarrier(1, &DrawRecordBarrier);
			}

			if (FrameGlobalsVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER FrameGlobalsBarrier = FrameGlobalsVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST);
				CommandList->ResourceBarrier(1, &FrameGlobalsBarrier);
			}

			if (MaterialVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER MaterialBarrier = MaterialVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST);
				CommandList->ResourceBarrier(1, &MaterialBarrier);
			}

			if (MaterialTextureTableVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER MaterialTextureTableBarrier = MaterialTextureTableVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST);
				CommandList->ResourceBarrier(1, &MaterialTextureTableBarrier);
			}
		}

		void DrawCallResourceManager::WaitForUpload(Interface::ICopyQueue& CopyQueue, uint32_t RtvIndex) const {
			uint64_t CopyFenceValue = mPerFrameCopyFenceValues[RtvIndex];
			if (CopyFenceValue == 0) {
				return;
			}

			CopyQueue.GuaranteeCopy(CopyFenceValue);
		}

		DescriptorHandle DrawCallResourceManager::GetFrameGlobalsSrvHandle(uint32_t RtvIndex) const {
			return mFrameGlobalsSrvHandles[RtvIndex];
		}

		DescriptorHandle DrawCallResourceManager::GetModelContextSrvHandle(uint32_t RtvIndex) const {
			return mModelContextSrvHandles[RtvIndex];
		}

		DescriptorHandle DrawCallResourceManager::GetDrawRecordSrvHandle(uint32_t RtvIndex) const {
			return mDrawRecordSrvHandles[RtvIndex];
		}

		DescriptorHandle DrawCallResourceManager::GetMaterialSrvHandle(uint32_t RtvIndex) const {
			return mMaterialSrvHandles[RtvIndex];
		}

		DescriptorHandle DrawCallResourceManager::GetMaterialTextureTableSrvHandle(uint32_t RtvIndex) const {
			return mMaterialTextureTableSrvHandles[RtvIndex];
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
			for (size_t Index = 0; Index < Data.drawRecords.size(); ++Index) {
				const Game::RFD::DrawRecord& SourceRecord = Data.drawRecords[Index];
				DrawRecordGPU& DestinationRecord = mDrawRecordsGpu[Index];
				DestinationRecord.ObjectIndex = SourceRecord.objectIndex;
				DestinationRecord.MaterialIndex = SourceRecord.materialIndex;
				DestinationRecord.Flags = SourceRecord.flags;
				DestinationRecord.Pad0 = SourceRecord.pad0;
			}
		}

		void DrawCallResourceManager::UpdateShaderResourceViews(uint32_t RtvIndex, uint32_t FrameGlobalsCount, uint32_t ModelContextCount, uint32_t DrawRecordCount, uint32_t MaterialCount, uint32_t MaterialTextureTableCount) {
			GraphicsVector& FrameGlobalsVector = mPerFrameFrameGlobalsVectors[RtvIndex];
			GraphicsVector& ModelContextVector = mPerFrameModelContextVectors[RtvIndex];
			GraphicsVector& DrawRecordVector = mPerFrameDrawRecordVectors[RtvIndex];
			GraphicsVector& MaterialVector = mPerFrameMaterialVectors[RtvIndex];
			GraphicsVector& MaterialTextureTableVector = mPerFrameMaterialTextureTableVectors[RtvIndex];
			ID3D12Resource* FrameGlobalsResource = FrameGlobalsVector.IsValid() == true ? FrameGlobalsVector.GetResource() : nullptr;
			ID3D12Resource* ModelContextResource = ModelContextVector.IsValid() == true ? ModelContextVector.GetResource() : nullptr;
			ID3D12Resource* DrawRecordResource = DrawRecordVector.IsValid() == true ? DrawRecordVector.GetResource() : nullptr;
			ID3D12Resource* MaterialResource = MaterialVector.IsValid() == true ? MaterialVector.GetResource() : nullptr;
			ID3D12Resource* MaterialTextureTableResource = MaterialTextureTableVector.IsValid() == true ? MaterialTextureTableVector.GetResource() : nullptr;

			if (FrameGlobalsVector.IsValid() == true) {
				bool IsUpdateRequired = DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mFrameGlobalsSrvResources[RtvIndex], FrameGlobalsResource, mFrameGlobalsSrvElementCounts[RtvIndex], FrameGlobalsCount);
				if (IsUpdateRequired == true) {
					FrameGlobalsVector.CreateShaderResourceView(mDevice, mFrameGlobalsSrvHandles[RtvIndex].GetCPU(), DXGI_FORMAT_UNKNOWN, 0, FrameGlobalsCount, sizeof(Game::RFD::FrameGlobals), D3D12_BUFFER_SRV_FLAG_NONE);
					mFrameGlobalsSrvResources[RtvIndex] = FrameGlobalsResource;
					mFrameGlobalsSrvElementCounts[RtvIndex] = FrameGlobalsCount;
				}
			}
			else {
				mFrameGlobalsSrvResources[RtvIndex] = nullptr;
				mFrameGlobalsSrvElementCounts[RtvIndex] = 0;
			}

			if (ModelContextVector.IsValid() == true) {
				bool IsUpdateRequired = DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mModelContextSrvResources[RtvIndex], ModelContextResource, mModelContextSrvElementCounts[RtvIndex], ModelContextCount);
				if (IsUpdateRequired == true) {
					ModelContextVector.CreateShaderResourceView(mDevice, mModelContextSrvHandles[RtvIndex].GetCPU(), DXGI_FORMAT_UNKNOWN, 0, ModelContextCount, sizeof(Game::RFD::ModelContext), D3D12_BUFFER_SRV_FLAG_NONE);
					mModelContextSrvResources[RtvIndex] = ModelContextResource;
					mModelContextSrvElementCounts[RtvIndex] = ModelContextCount;
				}
			}
			else {
				mModelContextSrvResources[RtvIndex] = nullptr;
				mModelContextSrvElementCounts[RtvIndex] = 0;
			}

			if (DrawRecordVector.IsValid() == true) {
				bool IsUpdateRequired = DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mDrawRecordSrvResources[RtvIndex], DrawRecordResource, mDrawRecordSrvElementCounts[RtvIndex], DrawRecordCount);
				if (IsUpdateRequired == true) {
					DrawRecordVector.CreateShaderResourceView(mDevice, mDrawRecordSrvHandles[RtvIndex].GetCPU(), DXGI_FORMAT_UNKNOWN, 0, DrawRecordCount, sizeof(DrawRecordGPU), D3D12_BUFFER_SRV_FLAG_NONE);
					mDrawRecordSrvResources[RtvIndex] = DrawRecordResource;
					mDrawRecordSrvElementCounts[RtvIndex] = DrawRecordCount;
				}
			}
			else {
				mDrawRecordSrvResources[RtvIndex] = nullptr;
				mDrawRecordSrvElementCounts[RtvIndex] = 0;
			}

			if (MaterialVector.IsValid() == true) {
				bool IsUpdateRequired = DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mMaterialSrvResources[RtvIndex], MaterialResource, mMaterialSrvElementCounts[RtvIndex], MaterialCount);
				if (IsUpdateRequired == true) {
					MaterialVector.CreateShaderResourceView(mDevice, mMaterialSrvHandles[RtvIndex].GetCPU(), DXGI_FORMAT_UNKNOWN, 0, MaterialCount, sizeof(Game::RFD::MaterialGpu), D3D12_BUFFER_SRV_FLAG_NONE);
					mMaterialSrvResources[RtvIndex] = MaterialResource;
					mMaterialSrvElementCounts[RtvIndex] = MaterialCount;
				}
			}
			else {
				mMaterialSrvResources[RtvIndex] = nullptr;
				mMaterialSrvElementCounts[RtvIndex] = 0;
			}

			if (MaterialTextureTableVector.IsValid() == true) {
				bool IsUpdateRequired = DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mMaterialTextureTableSrvResources[RtvIndex], MaterialTextureTableResource, mMaterialTextureTableSrvElementCounts[RtvIndex], MaterialTextureTableCount);
				if (IsUpdateRequired == true) {
					MaterialTextureTableVector.CreateShaderResourceView(mDevice, mMaterialTextureTableSrvHandles[RtvIndex].GetCPU(), DXGI_FORMAT_UNKNOWN, 0, MaterialTextureTableCount, sizeof(Game::RFD::MaterialTextureTableItemGpu), D3D12_BUFFER_SRV_FLAG_NONE);
					mMaterialTextureTableSrvResources[RtvIndex] = MaterialTextureTableResource;
					mMaterialTextureTableSrvElementCounts[RtvIndex] = MaterialTextureTableCount;
				}
			}
			else {
				mMaterialTextureTableSrvResources[RtvIndex] = nullptr;
				mMaterialTextureTableSrvElementCounts[RtvIndex] = 0;
			}
		}

		bool DrawCallResourceManager::IsShaderResourceViewUpdateRequired(ID3D12Resource* CachedResource, ID3D12Resource* CurrentResource, uint32_t CachedElementCount, uint32_t CurrentElementCount) const {
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
