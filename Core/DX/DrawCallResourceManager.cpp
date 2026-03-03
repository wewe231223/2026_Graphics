#include "DrawCallResourceManager.h"
#include <algorithm>
#include "Core/DX/GraphicsAllocator.h"
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
				mPerFrameCopyFenceValues[Index] = 0;
			}
		}

		void DrawCallResourceManager::PrepareFrameResources(uint32_t RtvIndex, Game::RFD::RenderFrameData& Data, GraphicsAllocator& GraphicsAllocator, Interface::ICopyQueue& CopyQueue) {
			std::stable_sort(Data.drawRecords.begin(), Data.drawRecords.end(), DrawCallResourceManager::CompareDrawRecordByPso);
			DrawCallResourceManager::BuildDrawRecordGpu(Data);

			GraphicsVector& FrameGlobalsVector = mPerFrameFrameGlobalsVectors[RtvIndex];
			GraphicsVector& ModelContextVector = mPerFrameModelContextVectors[RtvIndex];
			GraphicsVector& DrawRecordVector = mPerFrameDrawRecordVectors[RtvIndex];

			size_t FrameGlobalsSizeInBytes = sizeof(Game::RFD::FrameGlobals);
			size_t ModelContextsSizeInBytes = sizeof(Game::RFD::ModelContext) * Data.modelContexts.size();
			size_t DrawRecordsGpuSizeInBytes = sizeof(DrawRecordGPU) * mDrawRecordsGpu.size();

			std::byte DummyByte{ 0 };
			void* FrameGlobalsSourceData = FrameGlobalsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(&Data.globals);
			void* ModelContextSourceData = ModelContextsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(Data.modelContexts.data());
			void* DrawRecordSourceData = DrawRecordsGpuSizeInBytes == 0 ? &DummyByte : static_cast<void*>(mDrawRecordsGpu.data());

			bool FrameGlobalsCopyResult = FrameGlobalsVector.Copy(GraphicsAllocator, FrameGlobalsSourceData, FrameGlobalsSizeInBytes);
			ErrorHandler::report(FrameGlobalsCopyResult == false, "DrawCallResourceManager", "Failed to copy frame globals data.", ErrorHandler::Level::Critical);

			bool ModelContextCopyResult = ModelContextVector.Copy(GraphicsAllocator, ModelContextSourceData, ModelContextsSizeInBytes);
			ErrorHandler::report(ModelContextCopyResult == false, "DrawCallResourceManager", "Failed to copy model context data.", ErrorHandler::Level::Critical);

			bool DrawRecordCopyResult = DrawRecordVector.Copy(GraphicsAllocator, DrawRecordSourceData, DrawRecordsGpuSizeInBytes);
			ErrorHandler::report(DrawRecordCopyResult == false, "DrawCallResourceManager", "Failed to copy draw record data.", ErrorHandler::Level::Critical);

			std::array<Interface::CopyQueueCopyRequest, 3> CopyRequests{ FrameGlobalsVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0), ModelContextVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0), DrawRecordVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0) };
			uint64_t FenceValue = CopyQueue.EnqueueCopy(CopyRequests);
			CopyQueue.DispatchCopies();
			mPerFrameCopyFenceValues[RtvIndex] = FenceValue;

			DrawCallResourceManager::UpdateShaderResourceViews(RtvIndex, 1, static_cast<uint32_t>(Data.modelContexts.size()), static_cast<uint32_t>(mDrawRecordsGpu.size()));
		}

		void DrawCallResourceManager::TransitionToShaderResource(ID3D12GraphicsCommandList* CommandList, uint32_t RtvIndex) {
			GraphicsVector& FrameGlobalsVector = mPerFrameFrameGlobalsVectors[RtvIndex];
			GraphicsVector& ModelContextVector = mPerFrameModelContextVectors[RtvIndex];
			GraphicsVector& DrawRecordVector = mPerFrameDrawRecordVectors[RtvIndex];

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
		}

		void DrawCallResourceManager::TransitionToCopyDestination(ID3D12GraphicsCommandList* CommandList, uint32_t RtvIndex) {
			GraphicsVector& FrameGlobalsVector = mPerFrameFrameGlobalsVectors[RtvIndex];
			GraphicsVector& ModelContextVector = mPerFrameModelContextVectors[RtvIndex];
			GraphicsVector& DrawRecordVector = mPerFrameDrawRecordVectors[RtvIndex];

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
		}

		void DrawCallResourceManager::WaitForUpload(Interface::ICopyQueue& CopyQueue, uint32_t RtvIndex) const {
			uint64_t CopyFenceValue = mPerFrameCopyFenceValues[RtvIndex];
			if (CopyFenceValue != 0) {
				CopyQueue.WaitForFence(CopyFenceValue);
			}
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

			return Left.submesh < Right.submesh;
		}

		void DrawCallResourceManager::BuildDrawRecordGpu(const Game::RFD::RenderFrameData& Data) {
			mDrawRecordsGpu.clear();
			mDrawRecordsGpu.reserve(Data.drawRecords.size());

			for (const Game::RFD::DrawRecord& DrawRecord : Data.drawRecords) {
				DrawRecordGPU DrawRecordGpu{};
				DrawRecordGpu.ObjectIndex = DrawRecord.objectIndex;
				DrawRecordGpu.MaterialIndex = DrawRecord.materialIndex;
				DrawRecordGpu.Flags = DrawRecord.flags;
				DrawRecordGpu.Pad0 = DrawRecord.pad0;
				mDrawRecordsGpu.push_back(DrawRecordGpu);
			}
		}

		void DrawCallResourceManager::UpdateShaderResourceViews(uint32_t RtvIndex, uint32_t FrameGlobalsCount, uint32_t ModelContextCount, uint32_t DrawRecordCount) {
			GraphicsVector& FrameGlobalsVector = mPerFrameFrameGlobalsVectors[RtvIndex];
			GraphicsVector& ModelContextVector = mPerFrameModelContextVectors[RtvIndex];
			GraphicsVector& DrawRecordVector = mPerFrameDrawRecordVectors[RtvIndex];
			ID3D12Resource* FrameGlobalsResource = FrameGlobalsVector.GetResource();
			ID3D12Resource* ModelContextResource = ModelContextVector.GetResource();
			ID3D12Resource* DrawRecordResource = DrawRecordVector.GetResource();

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
