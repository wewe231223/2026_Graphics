#include "DrawCallResourceManager.h"
#include <algorithm>
#include <array>
#include <vector>
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
			mShadowFrameGlobalsSrvHandle = mSrvHeap->Allocate();
			mShadowMappingParameterSrvHandle = mSrvHeap->Allocate();
			mModelContextSrvHandle = mSrvHeap->Allocate();
			mBoundingBoxContextSrvHandle = mSrvHeap->Allocate();
			mDebugGeometryContextSrvHandle = mSrvHeap->Allocate();
			mBonePaletteSrvHandle = mSrvHeap->Allocate();
			mDrawRecordSrvHandle = mSrvHeap->Allocate();
			mCopyFuture = Interface::CopyFuture{};
			static_cast<void>(FrameIndex);
		}

		void DrawCallResourceManager::PrepareFrameResources(Game::RFD::RenderFrameData& Data, GraphicsAllocator& GraphicsAllocator, Interface::ICopyQueue* CopyQueue) {
			std::stable_sort(Data.drawRecords.begin(), Data.drawRecords.end(), DrawCallResourceManager::CompareDrawRecordByPso);
			DrawCallResourceManager::BuildDrawRecordGpu(Data);
			const std::uint32_t ShadowCascadeCount{ std::max<std::uint32_t>(1u, std::min<std::uint32_t>(Data.shadowMapping.cascadeCount, Game::RFD::ShadowCascadeMaxCount)) };
			std::array<Game::RFD::FrameGlobals, Game::RFD::ShadowCascadeMaxCount> ShadowFrameGlobalsArray{};
			for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1) {
				Game::RFD::FrameGlobals ShadowFrameGlobals{ Data.globals };
				ShadowFrameGlobals.view = Data.shadowMapping.shadowCameras[CascadeIndex].view;
				ShadowFrameGlobals.proj = Data.shadowMapping.shadowCameras[CascadeIndex].proj;
				ShadowFrameGlobals.viewProj = Data.shadowMapping.shadowCameras[CascadeIndex].viewProj;
				ShadowFrameGlobals.prevViewProj = ShadowFrameGlobals.viewProj;
				ShadowFrameGlobalsArray[CascadeIndex] = ShadowFrameGlobals;
			}

			std::size_t FrameGlobalsSizeInBytes{ sizeof(Game::RFD::FrameGlobals) };
			std::size_t ShadowFrameGlobalsSizeInBytes{ sizeof(Game::RFD::FrameGlobals) * static_cast<std::size_t>(ShadowCascadeCount) };
			std::size_t ShadowMappingParameterSizeInBytes{ sizeof(Game::RFD::ShadowMappingParameter) };
			std::size_t ModelContextsSizeInBytes{ sizeof(Game::RFD::ModelContext) * Data.modelContexts.size() };
			std::size_t BoundingBoxContextsSizeInBytes{ sizeof(Game::RFD::BoundingBoxContext) * Data.boundingBoxContexts.size() };
			std::size_t DebugGeometryContextsSizeInBytes{ sizeof(Game::RFD::DebugGeometryContext) * Data.debugGeometryContexts.size() };
			std::size_t BonePaletteSizeInBytes{ sizeof(SimpleMath::Matrix) * Data.bonePalette.size() };
			std::size_t DrawRecordsGpuSizeInBytes{ sizeof(DrawRecordGPU) * mDrawRecordsGpu.size() };

			std::byte DummyByte{ 0 };
			void* FrameGlobalsSourceData{ FrameGlobalsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(&Data.globals) };
			void* ShadowFrameGlobalsSourceData{ ShadowFrameGlobalsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(ShadowFrameGlobalsArray.data()) };
			void* ShadowMappingParameterSourceData{ ShadowMappingParameterSizeInBytes == 0 ? &DummyByte : static_cast<void*>(&Data.shadowMapping) };
			void* ModelContextSourceData{ ModelContextsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(Data.modelContexts.data()) };
			void* BoundingBoxContextSourceData{ BoundingBoxContextsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(Data.boundingBoxContexts.data()) };
			void* DebugGeometryContextSourceData{ DebugGeometryContextsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(Data.debugGeometryContexts.data()) };
			void* BonePaletteSourceData{ BonePaletteSizeInBytes == 0 ? &DummyByte : static_cast<void*>(Data.bonePalette.data()) };
			void* DrawRecordSourceData{ DrawRecordsGpuSizeInBytes == 0 ? &DummyByte : static_cast<void*>(mDrawRecordsGpu.data()) };

			bool FrameGlobalsCopyResult{ mFrameGlobalsVector.Copy(GraphicsAllocator, FrameGlobalsSourceData, FrameGlobalsSizeInBytes) };
			ErrorHandler::report(FrameGlobalsCopyResult == false, "DrawCallResourceManager", "Failed to copy frame globals data.", ErrorHandler::Level::Critical);

			bool ShadowFrameGlobalsCopyResult{ mShadowFrameGlobalsVector.Copy(GraphicsAllocator, ShadowFrameGlobalsSourceData, ShadowFrameGlobalsSizeInBytes) };
			ErrorHandler::report(ShadowFrameGlobalsCopyResult == false, "DrawCallResourceManager", "Failed to copy shadow frame globals data.", ErrorHandler::Level::Critical);

			bool ShadowMappingParameterCopyResult{ mShadowMappingParameterVector.Copy(GraphicsAllocator, ShadowMappingParameterSourceData, ShadowMappingParameterSizeInBytes) };
			ErrorHandler::report(ShadowMappingParameterCopyResult == false, "DrawCallResourceManager", "Failed to copy shadow mapping parameter data.", ErrorHandler::Level::Critical);

			bool ModelContextCopyResult{ mModelContextVector.Copy(GraphicsAllocator, ModelContextSourceData, ModelContextsSizeInBytes) };
			ErrorHandler::report(ModelContextCopyResult == false, "DrawCallResourceManager", "Failed to copy model context data.", ErrorHandler::Level::Critical);

			bool BoundingBoxContextCopyResult{ mBoundingBoxContextVector.Copy(GraphicsAllocator, BoundingBoxContextSourceData, BoundingBoxContextsSizeInBytes) };
			ErrorHandler::report(BoundingBoxContextCopyResult == false, "DrawCallResourceManager", "Failed to copy bounding box context data.", ErrorHandler::Level::Critical);

			bool DebugGeometryContextCopyResult{ mDebugGeometryContextVector.Copy(GraphicsAllocator, DebugGeometryContextSourceData, DebugGeometryContextsSizeInBytes) };
			ErrorHandler::report(DebugGeometryContextCopyResult == false, "DrawCallResourceManager", "Failed to copy debug geometry context data.", ErrorHandler::Level::Critical);

			bool BonePaletteCopyResult{ mBonePaletteVector.Copy(GraphicsAllocator, BonePaletteSourceData, BonePaletteSizeInBytes) };
			ErrorHandler::report(BonePaletteCopyResult == false, "DrawCallResourceManager", "Failed to copy bone palette data.", ErrorHandler::Level::Critical);

			bool DrawRecordCopyResult{ mDrawRecordVector.Copy(GraphicsAllocator, DrawRecordSourceData, DrawRecordsGpuSizeInBytes) };
			ErrorHandler::report(DrawRecordCopyResult == false, "DrawCallResourceManager", "Failed to copy draw record data.", ErrorHandler::Level::Critical);

			if (Data.drawRecords.empty() == true && Data.boundingBoxContexts.empty() == true && Data.debugGeometryContexts.empty() == true) {
				std::array<Interface::CopyQueueCopyRequest, 3> CopyRequests{ mFrameGlobalsVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0), mShadowFrameGlobalsVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0), mShadowMappingParameterVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0) };
				mCopyFuture = CopyQueue->EnqueueCopyFuture(CopyRequests);
				ErrorHandler::report(mCopyFuture.IsValid() == false, "DrawCallResourceManager", "Failed to enqueue frame upload copy requests.", ErrorHandler::Level::Critical);
			}
			else {
				std::array<Interface::CopyQueueCopyRequest, 8> CopyRequests{ mFrameGlobalsVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0), mShadowFrameGlobalsVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0), mShadowMappingParameterVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0), mModelContextVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0), mBoundingBoxContextVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0), mDebugGeometryContextVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0), mBonePaletteVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0), mDrawRecordVector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0) };
				mCopyFuture = CopyQueue->EnqueueCopyFuture(CopyRequests);
				ErrorHandler::report(mCopyFuture.IsValid() == false, "DrawCallResourceManager", "Failed to enqueue frame upload copy requests.", ErrorHandler::Level::Critical);
			}

			DrawCallResourceManager::UpdateShaderResourceViews(1, ShadowCascadeCount, 1, static_cast<std::uint32_t>(Data.modelContexts.size()), static_cast<std::uint32_t>(Data.boundingBoxContexts.size()), static_cast<std::uint32_t>(Data.debugGeometryContexts.size()), static_cast<std::uint32_t>(Data.bonePalette.size()), static_cast<std::uint32_t>(mDrawRecordsGpu.size()));
		}

		void DrawCallResourceManager::TransitionToShaderResource(ID3D12GraphicsCommandList* CommandList) {
			if (mFrameGlobalsVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER FrameGlobalsBarrier{ mFrameGlobalsVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) };
				CommandList->ResourceBarrier(1, &FrameGlobalsBarrier);
			}

			if (mShadowFrameGlobalsVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER ShadowFrameGlobalsBarrier{ mShadowFrameGlobalsVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) };
				CommandList->ResourceBarrier(1, &ShadowFrameGlobalsBarrier);
			}

			if (mShadowMappingParameterVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER ShadowMappingParameterBarrier{ mShadowMappingParameterVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) };
				CommandList->ResourceBarrier(1, &ShadowMappingParameterBarrier);
			}

			if (mModelContextVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER ModelContextBarrier{ mModelContextVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) };
				CommandList->ResourceBarrier(1, &ModelContextBarrier);
			}

			if (mBoundingBoxContextVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER BoundingBoxContextBarrier{ mBoundingBoxContextVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) };
				CommandList->ResourceBarrier(1, &BoundingBoxContextBarrier);
			}

			if (mDebugGeometryContextVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER DebugGeometryContextBarrier{ mDebugGeometryContextVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) };
				CommandList->ResourceBarrier(1, &DebugGeometryContextBarrier);
			}

			if (mBonePaletteVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER BonePaletteBarrier{ mBonePaletteVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) };
				CommandList->ResourceBarrier(1, &BonePaletteBarrier);
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

			if (mShadowFrameGlobalsVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER ShadowFrameGlobalsBarrier{ mShadowFrameGlobalsVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST) };
				CommandList->ResourceBarrier(1, &ShadowFrameGlobalsBarrier);
			}

			if (mShadowMappingParameterVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER ShadowMappingParameterBarrier{ mShadowMappingParameterVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST) };
				CommandList->ResourceBarrier(1, &ShadowMappingParameterBarrier);
			}

			if (mModelContextVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER ModelContextBarrier{ mModelContextVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST) };
				CommandList->ResourceBarrier(1, &ModelContextBarrier);
			}

			if (mBoundingBoxContextVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER BoundingBoxContextBarrier{ mBoundingBoxContextVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST) };
				CommandList->ResourceBarrier(1, &BoundingBoxContextBarrier);
			}

			if (mDebugGeometryContextVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER DebugGeometryContextBarrier{ mDebugGeometryContextVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST) };
				CommandList->ResourceBarrier(1, &DebugGeometryContextBarrier);
			}

			if (mBonePaletteVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER BonePaletteBarrier{ mBonePaletteVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST) };
				CommandList->ResourceBarrier(1, &BonePaletteBarrier);
			}

			if (mDrawRecordVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER DrawRecordBarrier{ mDrawRecordVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST) };
				CommandList->ResourceBarrier(1, &DrawRecordBarrier);
			}
		}

		void DrawCallResourceManager::WaitForUpload(Interface::ICopyQueue* CopyQueue) const {
			static_cast<void>(CopyQueue);

			if (mCopyFuture.IsValid() == false) {
				return;
			}

			mCopyFuture.Wait();
		}

		DescriptorHandle DrawCallResourceManager::GetFrameGlobalsSrvHandle() const {
			return mFrameGlobalsSrvHandle;
		}

		DescriptorHandle DrawCallResourceManager::GetShadowFrameGlobalsSrvHandle() const {
			return mShadowFrameGlobalsSrvHandle;
		}

		DescriptorHandle DrawCallResourceManager::GetShadowMappingParameterSrvHandle() const {
			return mShadowMappingParameterSrvHandle;
		}

		DescriptorHandle DrawCallResourceManager::GetModelContextSrvHandle() const {
			return mModelContextSrvHandle;
		}

		DescriptorHandle DrawCallResourceManager::GetBoundingBoxContextSrvHandle() const {
			return mBoundingBoxContextSrvHandle;
		}

		DescriptorHandle DrawCallResourceManager::GetDebugGeometryContextSrvHandle() const {
			return mDebugGeometryContextSrvHandle;
		}

		DescriptorHandle DrawCallResourceManager::GetBonePaletteSrvHandle() const {
			return mBonePaletteSrvHandle;
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

		void DrawCallResourceManager::UpdateShaderResourceViews(std::uint32_t FrameGlobalsCount, std::uint32_t ShadowFrameGlobalsCount, std::uint32_t ShadowMappingParameterCount, std::uint32_t ModelContextCount, std::uint32_t BoundingBoxContextCount, std::uint32_t DebugGeometryContextCount, std::uint32_t BonePaletteCount, std::uint32_t DrawRecordCount) {
			ID3D12Resource* FrameGlobalsResource{ mFrameGlobalsVector.IsValid() == true ? mFrameGlobalsVector.GetResource() : nullptr };
			ID3D12Resource* ShadowFrameGlobalsResource{ mShadowFrameGlobalsVector.IsValid() == true ? mShadowFrameGlobalsVector.GetResource() : nullptr };
			ID3D12Resource* ShadowMappingParameterResource{ mShadowMappingParameterVector.IsValid() == true ? mShadowMappingParameterVector.GetResource() : nullptr };
			ID3D12Resource* ModelContextResource{ mModelContextVector.IsValid() == true ? mModelContextVector.GetResource() : nullptr };
			ID3D12Resource* BoundingBoxContextResource{ mBoundingBoxContextVector.IsValid() == true ? mBoundingBoxContextVector.GetResource() : nullptr };
			ID3D12Resource* DebugGeometryContextResource{ mDebugGeometryContextVector.IsValid() == true ? mDebugGeometryContextVector.GetResource() : nullptr };
			ID3D12Resource* BonePaletteResource{ mBonePaletteVector.IsValid() == true ? mBonePaletteVector.GetResource() : nullptr };
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

			if (mShadowFrameGlobalsVector.IsValid() == true) {
				bool IsUpdateRequired{ DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mShadowFrameGlobalsSrvResource, ShadowFrameGlobalsResource, mShadowFrameGlobalsSrvElementCount, ShadowFrameGlobalsCount) };
				if (IsUpdateRequired == true) {
					mShadowFrameGlobalsVector.CreateShaderResourceView(mDevice, mShadowFrameGlobalsSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, ShadowFrameGlobalsCount, sizeof(Game::RFD::FrameGlobals), D3D12_BUFFER_SRV_FLAG_NONE);
					mShadowFrameGlobalsSrvResource = ShadowFrameGlobalsResource;
					mShadowFrameGlobalsSrvElementCount = ShadowFrameGlobalsCount;
				}
			}
			else {
				mShadowFrameGlobalsSrvResource = nullptr;
				mShadowFrameGlobalsSrvElementCount = 0;
			}

			if (mShadowMappingParameterVector.IsValid() == true) {
				bool IsUpdateRequired{ DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mShadowMappingParameterSrvResource, ShadowMappingParameterResource, mShadowMappingParameterSrvElementCount, ShadowMappingParameterCount) };
				if (IsUpdateRequired == true) {
					mShadowMappingParameterVector.CreateShaderResourceView(mDevice, mShadowMappingParameterSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, ShadowMappingParameterCount, sizeof(Game::RFD::ShadowMappingParameter), D3D12_BUFFER_SRV_FLAG_NONE);
					mShadowMappingParameterSrvResource = ShadowMappingParameterResource;
					mShadowMappingParameterSrvElementCount = ShadowMappingParameterCount;
				}
			}
			else {
				mShadowMappingParameterSrvResource = nullptr;
				mShadowMappingParameterSrvElementCount = 0;
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

			if (mBoundingBoxContextVector.IsValid() == true) {
				bool IsUpdateRequired{ DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mBoundingBoxContextSrvResource, BoundingBoxContextResource, mBoundingBoxContextSrvElementCount, BoundingBoxContextCount) };
				if (IsUpdateRequired == true) {
					mBoundingBoxContextVector.CreateShaderResourceView(mDevice, mBoundingBoxContextSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, BoundingBoxContextCount, sizeof(Game::RFD::BoundingBoxContext), D3D12_BUFFER_SRV_FLAG_NONE);
					mBoundingBoxContextSrvResource = BoundingBoxContextResource;
					mBoundingBoxContextSrvElementCount = BoundingBoxContextCount;
				}
			}
			else {
				mBoundingBoxContextSrvResource = nullptr;
				mBoundingBoxContextSrvElementCount = 0;
			}

			if (mDebugGeometryContextVector.IsValid() == true) {
				bool IsUpdateRequired{ DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mDebugGeometryContextSrvResource, DebugGeometryContextResource, mDebugGeometryContextSrvElementCount, DebugGeometryContextCount) };
				if (IsUpdateRequired == true) {
					mDebugGeometryContextVector.CreateShaderResourceView(mDevice, mDebugGeometryContextSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, DebugGeometryContextCount, sizeof(Game::RFD::DebugGeometryContext), D3D12_BUFFER_SRV_FLAG_NONE);
					mDebugGeometryContextSrvResource = DebugGeometryContextResource;
					mDebugGeometryContextSrvElementCount = DebugGeometryContextCount;
				}
			}
			else {
				mDebugGeometryContextSrvResource = nullptr;
				mDebugGeometryContextSrvElementCount = 0;
			}

			if (mBonePaletteVector.IsValid() == true) {
				bool IsUpdateRequired{ DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mBonePaletteSrvResource, BonePaletteResource, mBonePaletteSrvElementCount, BonePaletteCount) };
				if (IsUpdateRequired == true) {
					mBonePaletteVector.CreateShaderResourceView(mDevice, mBonePaletteSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, BonePaletteCount, sizeof(SimpleMath::Matrix), D3D12_BUFFER_SRV_FLAG_NONE);
					mBonePaletteSrvResource = BonePaletteResource;
					mBonePaletteSrvElementCount = BonePaletteCount;
				}
			}
			else {
				mBonePaletteSrvResource = nullptr;
				mBonePaletteSrvElementCount = 0;
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
