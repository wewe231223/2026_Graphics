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
			mTerrainPatchContextSrvHandle = mSrvHeap->Allocate();
			mBonePaletteSrvHandle = mSrvHeap->Allocate();
			mDrawRecordSrvHandle = mSrvHeap->Allocate();
			for (std::uint32_t ShadowCascadeIndex{ 0 }; ShadowCascadeIndex < Game::RFD::ShadowCascadeMaxCount; ShadowCascadeIndex += 1) {
				mShadowModelContextSrvHandles[ShadowCascadeIndex] = mSrvHeap->Allocate();
				mShadowTerrainPatchContextSrvHandles[ShadowCascadeIndex] = mSrvHeap->Allocate();
				mShadowDrawRecordSrvHandles[ShadowCascadeIndex] = mSrvHeap->Allocate();
			}

			mCopyFuture = Interface::CopyFuture{};
			static_cast<void>(FrameIndex);
		}

		void DrawCallResourceManager::PrepareFrameResources(Game::RFD::RenderFrameData& Data, GraphicsAllocator& GraphicsAllocator, Interface::ICopyQueue* CopyQueue) {
			std::stable_sort(Data.drawRecords.begin(), Data.drawRecords.end(), DrawCallResourceManager::CompareDrawRecordByPso);
			DrawCallResourceManager::BuildDrawRecordGpu(Data.drawRecords, mDrawRecordsGpu);
			const std::uint32_t ShadowCascadeCount{ Game::RFD::ResolveShadowCascadeCount(Data.shadowMapping) };
			for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1) {
				Game::RFD::ShadowRenderContext& ShadowRenderContext{ Data.ShadowRenderContexts[CascadeIndex] };
				DrawCallResourceManager::SortShadowDrawRecords(ShadowRenderContext.DrawRecords);
				DrawCallResourceManager::BuildDrawRecordGpu(ShadowRenderContext.DrawRecords, mShadowDrawRecordsGpu[CascadeIndex]);
			}

			for (std::uint32_t CascadeIndex{ ShadowCascadeCount }; CascadeIndex < Game::RFD::ShadowCascadeMaxCount; CascadeIndex += 1) {
				mShadowDrawRecordsGpu[CascadeIndex].clear();
			}

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
			std::size_t TerrainPatchContextsSizeInBytes{ sizeof(Game::RFD::TerrainPatchContext) * Data.TerrainPatchContexts.size() };
			std::size_t BonePaletteSizeInBytes{ sizeof(SimpleMath::Matrix) * Data.bonePalette.size() };
			std::size_t DrawRecordsGpuSizeInBytes{ sizeof(DrawRecordGPU) * mDrawRecordsGpu.size() };
			std::array<std::size_t, Game::RFD::ShadowCascadeMaxCount> ShadowModelContextsSizeInBytes{};
			std::array<std::size_t, Game::RFD::ShadowCascadeMaxCount> ShadowTerrainPatchContextsSizeInBytes{};
			std::array<std::size_t, Game::RFD::ShadowCascadeMaxCount> ShadowDrawRecordsGpuSizeInBytes{};
			for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1) {
				const Game::RFD::ShadowRenderContext& ShadowRenderContext{ Data.ShadowRenderContexts[CascadeIndex] };
				ShadowModelContextsSizeInBytes[CascadeIndex] = sizeof(Game::RFD::ModelContext) * ShadowRenderContext.ModelContexts.size();
				ShadowTerrainPatchContextsSizeInBytes[CascadeIndex] = sizeof(Game::RFD::TerrainPatchContext) * ShadowRenderContext.TerrainPatchContexts.size();
				ShadowDrawRecordsGpuSizeInBytes[CascadeIndex] = sizeof(DrawRecordGPU) * mShadowDrawRecordsGpu[CascadeIndex].size();
			}

			std::byte DummyByte{ 0 };
			void* FrameGlobalsSourceData{ FrameGlobalsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(&Data.globals) };
			void* ShadowFrameGlobalsSourceData{ ShadowFrameGlobalsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(ShadowFrameGlobalsArray.data()) };
			void* ShadowMappingParameterSourceData{ ShadowMappingParameterSizeInBytes == 0 ? &DummyByte : static_cast<void*>(&Data.shadowMapping) };
			void* ModelContextSourceData{ ModelContextsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(Data.modelContexts.data()) };
			void* BoundingBoxContextSourceData{ BoundingBoxContextsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(Data.boundingBoxContexts.data()) };
			void* DebugGeometryContextSourceData{ DebugGeometryContextsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(Data.debugGeometryContexts.data()) };
			void* TerrainPatchContextSourceData{ TerrainPatchContextsSizeInBytes == 0 ? &DummyByte : static_cast<void*>(Data.TerrainPatchContexts.data()) };
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

			bool TerrainPatchContextCopyResult{ mTerrainPatchContextVector.Copy(GraphicsAllocator, TerrainPatchContextSourceData, TerrainPatchContextsSizeInBytes) };
			ErrorHandler::report(TerrainPatchContextCopyResult == false, "DrawCallResourceManager", "Failed to copy terrain patch context data.", ErrorHandler::Level::Critical);

			bool BonePaletteCopyResult{ mBonePaletteVector.Copy(GraphicsAllocator, BonePaletteSourceData, BonePaletteSizeInBytes) };
			ErrorHandler::report(BonePaletteCopyResult == false, "DrawCallResourceManager", "Failed to copy bone palette data.", ErrorHandler::Level::Critical);

			bool DrawRecordCopyResult{ mDrawRecordVector.Copy(GraphicsAllocator, DrawRecordSourceData, DrawRecordsGpuSizeInBytes) };
			ErrorHandler::report(DrawRecordCopyResult == false, "DrawCallResourceManager", "Failed to copy draw record data.", ErrorHandler::Level::Critical);

			for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1) {
				Game::RFD::ShadowRenderContext& ShadowRenderContext{ Data.ShadowRenderContexts[CascadeIndex] };
				void* ShadowModelContextSourceData{ ShadowModelContextsSizeInBytes[CascadeIndex] == 0 ? &DummyByte : static_cast<void*>(ShadowRenderContext.ModelContexts.data()) };
				void* ShadowTerrainPatchContextSourceData{ ShadowTerrainPatchContextsSizeInBytes[CascadeIndex] == 0 ? &DummyByte : static_cast<void*>(ShadowRenderContext.TerrainPatchContexts.data()) };
				void* ShadowDrawRecordSourceData{ ShadowDrawRecordsGpuSizeInBytes[CascadeIndex] == 0 ? &DummyByte : static_cast<void*>(mShadowDrawRecordsGpu[CascadeIndex].data()) };

				bool ShadowModelContextCopyResult{ mShadowModelContextVectors[CascadeIndex].Copy(GraphicsAllocator, ShadowModelContextSourceData, ShadowModelContextsSizeInBytes[CascadeIndex]) };
				ErrorHandler::report(ShadowModelContextCopyResult == false, "DrawCallResourceManager", "Failed to copy shadow model context data.", ErrorHandler::Level::Critical);

				bool ShadowTerrainPatchContextCopyResult{ mShadowTerrainPatchContextVectors[CascadeIndex].Copy(GraphicsAllocator, ShadowTerrainPatchContextSourceData, ShadowTerrainPatchContextsSizeInBytes[CascadeIndex]) };
				ErrorHandler::report(ShadowTerrainPatchContextCopyResult == false, "DrawCallResourceManager", "Failed to copy shadow terrain patch context data.", ErrorHandler::Level::Critical);

				bool ShadowDrawRecordCopyResult{ mShadowDrawRecordVectors[CascadeIndex].Copy(GraphicsAllocator, ShadowDrawRecordSourceData, ShadowDrawRecordsGpuSizeInBytes[CascadeIndex]) };
				ErrorHandler::report(ShadowDrawRecordCopyResult == false, "DrawCallResourceManager", "Failed to copy shadow draw record data.", ErrorHandler::Level::Critical);
			}

			std::vector<Interface::CopyQueueCopyRequest> CopyRequests{};
			CopyRequests.reserve(9ULL + (static_cast<std::size_t>(ShadowCascadeCount) * 3ULL));

			auto AddCopyRequestIfValid{ [&CopyRequests, &GraphicsAllocator](GraphicsVector& Vector) {
				if (Vector.IsValid() == false) {
					return;
				}

				CopyRequests.push_back(Vector.CreateCopyQueueCopyRequest(GraphicsAllocator, 0));
			} };

			AddCopyRequestIfValid(mFrameGlobalsVector);
			AddCopyRequestIfValid(mShadowFrameGlobalsVector);
			AddCopyRequestIfValid(mShadowMappingParameterVector);
			AddCopyRequestIfValid(mModelContextVector);
			AddCopyRequestIfValid(mBoundingBoxContextVector);
			AddCopyRequestIfValid(mDebugGeometryContextVector);
			AddCopyRequestIfValid(mTerrainPatchContextVector);
			AddCopyRequestIfValid(mBonePaletteVector);
			AddCopyRequestIfValid(mDrawRecordVector);
			for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1) {
				AddCopyRequestIfValid(mShadowModelContextVectors[CascadeIndex]);
				AddCopyRequestIfValid(mShadowTerrainPatchContextVectors[CascadeIndex]);
				AddCopyRequestIfValid(mShadowDrawRecordVectors[CascadeIndex]);
			}

			mCopyFuture = CopyQueue->EnqueueCopyFuture(CopyRequests);
			ErrorHandler::report(mCopyFuture.IsValid() == false, "DrawCallResourceManager", "Failed to enqueue frame upload copy requests.", ErrorHandler::Level::Critical);

			DrawCallResourceManager::UpdateShaderResourceViews(1, ShadowCascadeCount, 1, static_cast<std::uint32_t>(Data.modelContexts.size()), static_cast<std::uint32_t>(Data.boundingBoxContexts.size()), static_cast<std::uint32_t>(Data.debugGeometryContexts.size()), static_cast<std::uint32_t>(Data.TerrainPatchContexts.size()), static_cast<std::uint32_t>(Data.bonePalette.size()), static_cast<std::uint32_t>(mDrawRecordsGpu.size()));
			DrawCallResourceManager::UpdateShadowShaderResourceViews(ShadowCascadeCount);
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

			if (mTerrainPatchContextVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER TerrainPatchContextBarrier{ mTerrainPatchContextVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) };
				CommandList->ResourceBarrier(1, &TerrainPatchContextBarrier);
			}

			if (mBonePaletteVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER BonePaletteBarrier{ mBonePaletteVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) };
				CommandList->ResourceBarrier(1, &BonePaletteBarrier);
			}

			if (mDrawRecordVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER DrawRecordBarrier{ mDrawRecordVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) };
				CommandList->ResourceBarrier(1, &DrawRecordBarrier);
			}

			for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < Game::RFD::ShadowCascadeMaxCount; CascadeIndex += 1) {
				if (mShadowModelContextVectors[CascadeIndex].IsValid() == true) {
					D3D12_RESOURCE_BARRIER ShadowModelContextBarrier{ mShadowModelContextVectors[CascadeIndex].CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) };
					CommandList->ResourceBarrier(1, &ShadowModelContextBarrier);
				}

				if (mShadowTerrainPatchContextVectors[CascadeIndex].IsValid() == true) {
					D3D12_RESOURCE_BARRIER ShadowTerrainPatchContextBarrier{ mShadowTerrainPatchContextVectors[CascadeIndex].CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) };
					CommandList->ResourceBarrier(1, &ShadowTerrainPatchContextBarrier);
				}

				if (mShadowDrawRecordVectors[CascadeIndex].IsValid() == true) {
					D3D12_RESOURCE_BARRIER ShadowDrawRecordBarrier{ mShadowDrawRecordVectors[CascadeIndex].CreateTransitionBarrier(D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) };
					CommandList->ResourceBarrier(1, &ShadowDrawRecordBarrier);
				}
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

			if (mTerrainPatchContextVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER TerrainPatchContextBarrier{ mTerrainPatchContextVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST) };
				CommandList->ResourceBarrier(1, &TerrainPatchContextBarrier);
			}

			if (mBonePaletteVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER BonePaletteBarrier{ mBonePaletteVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST) };
				CommandList->ResourceBarrier(1, &BonePaletteBarrier);
			}

			if (mDrawRecordVector.IsValid() == true) {
				D3D12_RESOURCE_BARRIER DrawRecordBarrier{ mDrawRecordVector.CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST) };
				CommandList->ResourceBarrier(1, &DrawRecordBarrier);
			}

			for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < Game::RFD::ShadowCascadeMaxCount; CascadeIndex += 1) {
				if (mShadowModelContextVectors[CascadeIndex].IsValid() == true) {
					D3D12_RESOURCE_BARRIER ShadowModelContextBarrier{ mShadowModelContextVectors[CascadeIndex].CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST) };
					CommandList->ResourceBarrier(1, &ShadowModelContextBarrier);
				}

				if (mShadowTerrainPatchContextVectors[CascadeIndex].IsValid() == true) {
					D3D12_RESOURCE_BARRIER ShadowTerrainPatchContextBarrier{ mShadowTerrainPatchContextVectors[CascadeIndex].CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST) };
					CommandList->ResourceBarrier(1, &ShadowTerrainPatchContextBarrier);
				}

				if (mShadowDrawRecordVectors[CascadeIndex].IsValid() == true) {
					D3D12_RESOURCE_BARRIER ShadowDrawRecordBarrier{ mShadowDrawRecordVectors[CascadeIndex].CreateTransitionBarrier(D3D12_RESOURCE_STATE_COPY_DEST) };
					CommandList->ResourceBarrier(1, &ShadowDrawRecordBarrier);
				}
			}
		}

		void DrawCallResourceManager::QueueWaitForUpload(ID3D12CommandQueue* WaitingQueue) const {
			if (mCopyFuture.IsValid() == false) {
				return;
			}

			mCopyFuture.QueueWait(WaitingQueue);
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

		DescriptorHandle DrawCallResourceManager::GetTerrainPatchContextSrvHandle() const {
			return mTerrainPatchContextSrvHandle;
		}

		DescriptorHandle DrawCallResourceManager::GetBonePaletteSrvHandle() const {
			return mBonePaletteSrvHandle;
		}

		DescriptorHandle DrawCallResourceManager::GetDrawRecordSrvHandle() const {
			return mDrawRecordSrvHandle;
		}

		DescriptorHandle DrawCallResourceManager::GetShadowModelContextSrvHandle(std::uint32_t ShadowCascadeIndex) const {
			const std::uint32_t ClampedShadowCascadeIndex{ std::min<std::uint32_t>(ShadowCascadeIndex, Game::RFD::ShadowCascadeMaxCount - 1u) };
			return mShadowModelContextSrvHandles[ClampedShadowCascadeIndex];
		}

		DescriptorHandle DrawCallResourceManager::GetShadowTerrainPatchContextSrvHandle(std::uint32_t ShadowCascadeIndex) const {
			const std::uint32_t ClampedShadowCascadeIndex{ std::min<std::uint32_t>(ShadowCascadeIndex, Game::RFD::ShadowCascadeMaxCount - 1u) };
			return mShadowTerrainPatchContextSrvHandles[ClampedShadowCascadeIndex];
		}

		DescriptorHandle DrawCallResourceManager::GetShadowDrawRecordSrvHandle(std::uint32_t ShadowCascadeIndex) const {
			const std::uint32_t ClampedShadowCascadeIndex{ std::min<std::uint32_t>(ShadowCascadeIndex, Game::RFD::ShadowCascadeMaxCount - 1u) };
			return mShadowDrawRecordSrvHandles[ClampedShadowCascadeIndex];
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

		void DrawCallResourceManager::SortShadowDrawRecords(std::vector<Game::RFD::DrawRecord>& DrawRecords) {
			if (DrawRecords.size() < 2ULL) {
				return;
			}

			const bool IsSorted{ DrawCallResourceManager::IsDrawRecordsSorted(DrawRecords) };
			if (IsSorted == true) {
				return;
			}

			std::sort(DrawRecords.begin(), DrawRecords.end(), DrawCallResourceManager::CompareDrawRecordByPso);
		}

		bool DrawCallResourceManager::IsDrawRecordsSorted(const std::vector<Game::RFD::DrawRecord>& DrawRecords) {
			return std::is_sorted(DrawRecords.begin(), DrawRecords.end(), DrawCallResourceManager::CompareDrawRecordByPso);
		}

		void DrawCallResourceManager::BuildDrawRecordGpu(const std::vector<Game::RFD::DrawRecord>& DrawRecords, std::vector<DrawRecordGPU>& OutDrawRecordsGpu) {
			OutDrawRecordsGpu.resize(DrawRecords.size());
			for (std::size_t Index{ 0 }; Index < DrawRecords.size(); ++Index) {
				const Game::RFD::DrawRecord& SourceRecord{ DrawRecords[Index] };
				DrawRecordGPU& DestinationRecord{ OutDrawRecordsGpu[Index] };
				DestinationRecord.ObjectIndex = SourceRecord.objectIndex;
				DestinationRecord.MaterialIndex = SourceRecord.materialIndex;
				DestinationRecord.Flags = SourceRecord.flags;
				DestinationRecord.TerrainPatchContextIndex = SourceRecord.TerrainPatchContextIndex;
			}
		}

		void DrawCallResourceManager::UpdateShaderResourceViews(std::uint32_t FrameGlobalsCount, std::uint32_t ShadowFrameGlobalsCount, std::uint32_t ShadowMappingParameterCount, std::uint32_t ModelContextCount, std::uint32_t BoundingBoxContextCount, std::uint32_t DebugGeometryContextCount, std::uint32_t TerrainPatchContextCount, std::uint32_t BonePaletteCount, std::uint32_t DrawRecordCount) {
			ID3D12Resource* FrameGlobalsResource{ mFrameGlobalsVector.IsValid() == true ? mFrameGlobalsVector.GetResource() : nullptr };
			ID3D12Resource* ShadowFrameGlobalsResource{ mShadowFrameGlobalsVector.IsValid() == true ? mShadowFrameGlobalsVector.GetResource() : nullptr };
			ID3D12Resource* ShadowMappingParameterResource{ mShadowMappingParameterVector.IsValid() == true ? mShadowMappingParameterVector.GetResource() : nullptr };
			ID3D12Resource* ModelContextResource{ mModelContextVector.IsValid() == true ? mModelContextVector.GetResource() : nullptr };
			ID3D12Resource* BoundingBoxContextResource{ mBoundingBoxContextVector.IsValid() == true ? mBoundingBoxContextVector.GetResource() : nullptr };
			ID3D12Resource* DebugGeometryContextResource{ mDebugGeometryContextVector.IsValid() == true ? mDebugGeometryContextVector.GetResource() : nullptr };
			ID3D12Resource* TerrainPatchContextResource{ mTerrainPatchContextVector.IsValid() == true ? mTerrainPatchContextVector.GetResource() : nullptr };
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

			if (mTerrainPatchContextVector.IsValid() == true) {
				bool IsUpdateRequired{ DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mTerrainPatchContextSrvResource, TerrainPatchContextResource, mTerrainPatchContextSrvElementCount, TerrainPatchContextCount) };
				if (IsUpdateRequired == true) {
					mTerrainPatchContextVector.CreateShaderResourceView(mDevice, mTerrainPatchContextSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, TerrainPatchContextCount, sizeof(Game::RFD::TerrainPatchContext), D3D12_BUFFER_SRV_FLAG_NONE);
					mTerrainPatchContextSrvResource = TerrainPatchContextResource;
					mTerrainPatchContextSrvElementCount = TerrainPatchContextCount;
				}
			}
			else {
				mTerrainPatchContextSrvResource = nullptr;
				mTerrainPatchContextSrvElementCount = 0;
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

		void DrawCallResourceManager::UpdateShadowShaderResourceViews(std::uint32_t ShadowCascadeCount) {
			for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < Game::RFD::ShadowCascadeMaxCount; CascadeIndex += 1) {
				GraphicsVector& ShadowModelContextVector{ mShadowModelContextVectors[CascadeIndex] };
				GraphicsVector& ShadowTerrainPatchContextVector{ mShadowTerrainPatchContextVectors[CascadeIndex] };
				GraphicsVector& ShadowDrawRecordVector{ mShadowDrawRecordVectors[CascadeIndex] };
				const bool IsActiveCascade{ CascadeIndex < ShadowCascadeCount };
				ID3D12Resource* ShadowModelContextResource{ ShadowModelContextVector.IsValid() == true ? ShadowModelContextVector.GetResource() : nullptr };
				ID3D12Resource* ShadowTerrainPatchContextResource{ ShadowTerrainPatchContextVector.IsValid() == true ? ShadowTerrainPatchContextVector.GetResource() : nullptr };
				ID3D12Resource* ShadowDrawRecordResource{ ShadowDrawRecordVector.IsValid() == true ? ShadowDrawRecordVector.GetResource() : nullptr };
				const std::uint32_t ShadowModelContextCount{ IsActiveCascade == true ? static_cast<std::uint32_t>(ShadowModelContextVector.GetSizeInBytes() / sizeof(Game::RFD::ModelContext)) : 0u };
				const std::uint32_t ShadowTerrainPatchContextCount{ IsActiveCascade == true ? static_cast<std::uint32_t>(ShadowTerrainPatchContextVector.GetSizeInBytes() / sizeof(Game::RFD::TerrainPatchContext)) : 0u };
				const std::uint32_t ShadowDrawRecordCount{ IsActiveCascade == true ? static_cast<std::uint32_t>(ShadowDrawRecordVector.GetSizeInBytes() / sizeof(DrawRecordGPU)) : 0u };

				if (IsActiveCascade == true && ShadowModelContextVector.IsValid() == true) {
					bool IsUpdateRequired{ DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mShadowModelContextSrvResources[CascadeIndex], ShadowModelContextResource, mShadowModelContextSrvElementCounts[CascadeIndex], ShadowModelContextCount) };
					if (IsUpdateRequired == true) {
						ShadowModelContextVector.CreateShaderResourceView(mDevice, mShadowModelContextSrvHandles[CascadeIndex].GetCPU(), DXGI_FORMAT_UNKNOWN, 0, ShadowModelContextCount, sizeof(Game::RFD::ModelContext), D3D12_BUFFER_SRV_FLAG_NONE);
						mShadowModelContextSrvResources[CascadeIndex] = ShadowModelContextResource;
						mShadowModelContextSrvElementCounts[CascadeIndex] = ShadowModelContextCount;
					}
				}
				else {
					mShadowModelContextSrvResources[CascadeIndex] = nullptr;
					mShadowModelContextSrvElementCounts[CascadeIndex] = 0;
				}

				if (IsActiveCascade == true && ShadowTerrainPatchContextVector.IsValid() == true) {
					bool IsUpdateRequired{ DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mShadowTerrainPatchContextSrvResources[CascadeIndex], ShadowTerrainPatchContextResource, mShadowTerrainPatchContextSrvElementCounts[CascadeIndex], ShadowTerrainPatchContextCount) };
					if (IsUpdateRequired == true) {
						ShadowTerrainPatchContextVector.CreateShaderResourceView(mDevice, mShadowTerrainPatchContextSrvHandles[CascadeIndex].GetCPU(), DXGI_FORMAT_UNKNOWN, 0, ShadowTerrainPatchContextCount, sizeof(Game::RFD::TerrainPatchContext), D3D12_BUFFER_SRV_FLAG_NONE);
						mShadowTerrainPatchContextSrvResources[CascadeIndex] = ShadowTerrainPatchContextResource;
						mShadowTerrainPatchContextSrvElementCounts[CascadeIndex] = ShadowTerrainPatchContextCount;
					}
				}
				else {
					mShadowTerrainPatchContextSrvResources[CascadeIndex] = nullptr;
					mShadowTerrainPatchContextSrvElementCounts[CascadeIndex] = 0;
				}

				if (IsActiveCascade == true && ShadowDrawRecordVector.IsValid() == true) {
					bool IsUpdateRequired{ DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mShadowDrawRecordSrvResources[CascadeIndex], ShadowDrawRecordResource, mShadowDrawRecordSrvElementCounts[CascadeIndex], ShadowDrawRecordCount) };
					if (IsUpdateRequired == true) {
						ShadowDrawRecordVector.CreateShaderResourceView(mDevice, mShadowDrawRecordSrvHandles[CascadeIndex].GetCPU(), DXGI_FORMAT_UNKNOWN, 0, ShadowDrawRecordCount, sizeof(DrawRecordGPU), D3D12_BUFFER_SRV_FLAG_NONE);
						mShadowDrawRecordSrvResources[CascadeIndex] = ShadowDrawRecordResource;
						mShadowDrawRecordSrvElementCounts[CascadeIndex] = ShadowDrawRecordCount;
					}
				}
				else {
					mShadowDrawRecordSrvResources[CascadeIndex] = nullptr;
					mShadowDrawRecordSrvElementCounts[CascadeIndex] = 0;
				}
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
