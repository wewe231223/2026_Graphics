#include "DrawCallResourceManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <tuple>
#include <vector>
#include "Core/DX/GraphicsAllocator.h"
#include "Utility/ErrorHandler.h"

namespace Core {
	namespace DX {
		namespace {
			SimpleMath::Matrix BuildGpuMatrix(const SimpleMath::Matrix& SourceMatrix) {
				return SourceMatrix.Transpose();
			}

			SimpleMath::Vector3 ResolveDirectionalLightDirection(const SimpleMath::Vector4& Direction) {
				const float DirectionLengthSquared{ (Direction.x * Direction.x) + (Direction.y * Direction.y) + (Direction.z * Direction.z) };
				if (DirectionLengthSquared <= 0.0f) {
					const SimpleMath::Vector3 DefaultDirection{ 0.4f, -1.0f, 0.35f };
					const float DefaultDirectionLengthInverse{ 1.0f / std::sqrt((DefaultDirection.x * DefaultDirection.x) + (DefaultDirection.y * DefaultDirection.y) + (DefaultDirection.z * DefaultDirection.z)) };
					return SimpleMath::Vector3{ DefaultDirection.x * DefaultDirectionLengthInverse, DefaultDirection.y * DefaultDirectionLengthInverse, DefaultDirection.z * DefaultDirectionLengthInverse };
				}

				const float DirectionLengthInverse{ 1.0f / std::sqrt(DirectionLengthSquared) };
				return SimpleMath::Vector3{ Direction.x * DirectionLengthInverse, Direction.y * DirectionLengthInverse, Direction.z * DirectionLengthInverse };
			}

			RenderContract::FrameGlobals BuildGpuFrameGlobals(const RenderContract::FrameGlobals& SourceFrameGlobals) {
				RenderContract::FrameGlobals GpuFrameGlobals{ SourceFrameGlobals };
				GpuFrameGlobals.mView = BuildGpuMatrix(SourceFrameGlobals.mView);
				GpuFrameGlobals.mProj = BuildGpuMatrix(SourceFrameGlobals.mProj);
				GpuFrameGlobals.mViewProj = BuildGpuMatrix(SourceFrameGlobals.mViewProj);
				GpuFrameGlobals.mPrevViewProj = BuildGpuMatrix(SourceFrameGlobals.mPrevViewProj);
				return GpuFrameGlobals;
			}

			RenderContract::CameraParameter BuildGpuCameraParameter(const RenderContract::CameraParameter& SourceCameraParameter) {
				RenderContract::CameraParameter GpuCameraParameter{ SourceCameraParameter };
				GpuCameraParameter.mView = BuildGpuMatrix(SourceCameraParameter.mView);
				GpuCameraParameter.mProj = BuildGpuMatrix(SourceCameraParameter.mProj);
				GpuCameraParameter.mViewProj = BuildGpuMatrix(SourceCameraParameter.mViewProj);
				return GpuCameraParameter;
			}

			RenderContract::DirectionalLightParameter BuildGpuDirectionalLightParameter(const RenderContract::DirectionalLightParameter& SourceDirectionalLightParameter) {
				RenderContract::DirectionalLightParameter GpuDirectionalLightParameter{ SourceDirectionalLightParameter };
				const SimpleMath::Vector3 Direction{ ResolveDirectionalLightDirection(SourceDirectionalLightParameter.mDirection) };
				GpuDirectionalLightParameter.mDirection = SimpleMath::Vector4{ Direction.x, Direction.y, Direction.z, SourceDirectionalLightParameter.mDirection.w };
				return GpuDirectionalLightParameter;
			}

			RenderContract::ShadowMappingParameter BuildGpuShadowMappingParameter(const RenderContract::ShadowMappingParameter& SourceShadowMappingParameter) {
				RenderContract::ShadowMappingParameter GpuShadowMappingParameter{ SourceShadowMappingParameter };
				GpuShadowMappingParameter.mDirectionalLight = BuildGpuDirectionalLightParameter(SourceShadowMappingParameter.mDirectionalLight);
				for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < RenderContract::ShadowCascadeMaxCount; CascadeIndex += 1) {
					GpuShadowMappingParameter.mShadowCameras[CascadeIndex] = BuildGpuCameraParameter(SourceShadowMappingParameter.mShadowCameras[CascadeIndex]);
				}

				return GpuShadowMappingParameter;
			}

			RenderContract::ModelContext BuildGpuModelContext(const RenderContract::ModelContext& SourceModelContext) {
				RenderContract::ModelContext GpuModelContext{ SourceModelContext };
				GpuModelContext.mWorld = BuildGpuMatrix(SourceModelContext.mWorld);
				GpuModelContext.mPrevWorld = BuildGpuMatrix(SourceModelContext.mPrevWorld);
				return GpuModelContext;
			}

			void BuildGpuModelContexts(const std::vector<RenderContract::ModelContext>& SourceModelContexts, std::vector<RenderContract::ModelContext>& OutGpuModelContexts) {
				OutGpuModelContexts.resize(SourceModelContexts.size());
				for (std::size_t Index{ 0 }; Index < SourceModelContexts.size(); ++Index) {
					OutGpuModelContexts[Index] = BuildGpuModelContext(SourceModelContexts[Index]);
				}
			}

			void BuildGpuBonePalette(const std::vector<SimpleMath::Matrix>& SourceBonePalette, std::vector<SimpleMath::Matrix>& OutGpuBonePalette) {
				OutGpuBonePalette.resize(SourceBonePalette.size());
				for (std::size_t Index{ 0 }; Index < SourceBonePalette.size(); ++Index) {
					OutGpuBonePalette[Index] = BuildGpuMatrix(SourceBonePalette[Index]);
				}
			}

			RenderContract::EnvironmentSegmentContext BuildGpuEnvironmentSegmentContext(const RenderContract::EnvironmentSegmentContext& SourceSegmentContext) {
				RenderContract::EnvironmentSegmentContext GpuSegmentContext{ SourceSegmentContext };
				GpuSegmentContext.mLocalTransform = BuildGpuMatrix(SourceSegmentContext.mLocalTransform);
				return GpuSegmentContext;
			}

			void BuildGpuEnvironmentSegmentContexts(const std::vector<RenderContract::EnvironmentSegmentContext>& SourceSegmentContexts, std::vector<RenderContract::EnvironmentSegmentContext>& OutGpuSegmentContexts) {
				OutGpuSegmentContexts.resize(SourceSegmentContexts.size());
				for (std::size_t Index{ 0 }; Index < SourceSegmentContexts.size(); ++Index) {
					OutGpuSegmentContexts[Index] = BuildGpuEnvironmentSegmentContext(SourceSegmentContexts[Index]);
				}
			}

			template <typename T>
			std::span<const std::byte> MakeByteSpan(const T& Value) {
				return std::as_bytes(std::span<const T>{ &Value, 1 });
			}

			template <typename T>
			std::span<const std::byte> MakeByteSpan(const std::vector<T>& Values) {
				return std::as_bytes(std::span<const T>{ Values.data(), Values.size() });
			}

			template <typename T, std::size_t Count>
			std::span<const std::byte> MakeByteSpan(const std::array<T, Count>& Values, std::size_t ActiveCount) {
				return std::as_bytes(std::span<const T>{ Values.data(), ActiveCount });
			}

			void AddGraphicsVectorCopyRequest(GraphicsVector& Vector, GraphicsAllocator& GraphicsAllocatorValue, std::span<const std::byte> SourceData, std::vector<Interface::CopyRequest>& CopyRequests, const char* FailureMessage) {
				Interface::CopyRequest CopyRequest{ Interface::CopyPriority::High };
				bool PrepareResult{ Vector.PrepareCopyRequest(GraphicsAllocatorValue, SourceData, Interface::CopyPriority::High, CopyRequest, 0) };
				ErrorHandler::report(PrepareResult == false, "DrawCallResourceManager", FailureMessage, ErrorHandler::Level::Critical);
				if (PrepareResult == true and CopyRequest.SourceData.empty() == false and CopyRequest.DestinationDefaultResource != nullptr) {
					CopyRequests.push_back(CopyRequest);
				}
			}
		}

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
			mEnvironmentInstanceContextSrvHandle = mSrvHeap->Allocate();
			mEnvironmentSegmentContextSrvHandle = mSrvHeap->Allocate();
			mEnvironmentDrawRecordSrvHandle = mSrvHeap->Allocate();
			for (std::uint32_t ShadowCascadeIndex{ 0 }; ShadowCascadeIndex < RenderContract::ShadowCascadeMaxCount; ShadowCascadeIndex += 1) {
				mShadowModelContextSrvHandles[ShadowCascadeIndex] = mSrvHeap->Allocate();
				mShadowTerrainPatchContextSrvHandles[ShadowCascadeIndex] = mSrvHeap->Allocate();
				mShadowDrawRecordSrvHandles[ShadowCascadeIndex] = mSrvHeap->Allocate();
				mShadowEnvironmentDrawRecordSrvHandles[ShadowCascadeIndex] = mSrvHeap->Allocate();
			}

			mCopyFuture = RenderContract::Future{};
			static_cast<void>(FrameIndex);
		}

		void DrawCallResourceManager::PrepareFrameResources(RenderContract::RenderFrameData& Data, GraphicsAllocator& GraphicsAllocator, Interface::ICopyQueue* CopyQueue) {
			const bool IsEnvironmentGpuDrivenFrameEnabled{ Data.mEnvironmentGpuDrivenFrame.mEnabled == true };
			std::stable_sort(Data.mDrawRecords.begin(), Data.mDrawRecords.end(), DrawCallResourceManager::CompareDrawRecordByPso);
			DrawCallResourceManager::BuildDrawRecordGpu(Data.mDrawRecords, mDrawRecordsGpu);
			if (IsEnvironmentGpuDrivenFrameEnabled == false) {
				std::stable_sort(Data.mEnvironmentDrawRecords.begin(), Data.mEnvironmentDrawRecords.end(), DrawCallResourceManager::CompareEnvironmentDrawRecordByPso);
				DrawCallResourceManager::BuildEnvironmentDrawRecordGpu(Data.mEnvironmentDrawRecords, mEnvironmentDrawRecordsGpu);
			}
			else {
				mEnvironmentDrawRecordsGpu.clear();
			}

			const std::uint32_t ShadowCascadeCount{ RenderContract::ResolveShadowCascadeCount(Data.mShadowMappingParameter) };
			for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1) {
				RenderContract::ShadowRenderContext& ShadowRenderContext{ Data.mShadowRenderContexts[CascadeIndex] };
				DrawCallResourceManager::SortShadowDrawRecords(ShadowRenderContext.mDrawRecords);
				DrawCallResourceManager::BuildDrawRecordGpu(ShadowRenderContext.mDrawRecords, mShadowDrawRecordsGpu[CascadeIndex]);
				if (IsEnvironmentGpuDrivenFrameEnabled == false) {
					DrawCallResourceManager::SortShadowEnvironmentDrawRecords(ShadowRenderContext.mEnvironmentDrawRecords);
					DrawCallResourceManager::BuildEnvironmentDrawRecordGpu(ShadowRenderContext.mEnvironmentDrawRecords, mShadowEnvironmentDrawRecordsGpu[CascadeIndex]);
				}
				else {
					mShadowEnvironmentDrawRecordsGpu[CascadeIndex].clear();
				}
			}

			for (std::uint32_t CascadeIndex{ ShadowCascadeCount }; CascadeIndex < RenderContract::ShadowCascadeMaxCount; CascadeIndex += 1) {
				mShadowDrawRecordsGpu[CascadeIndex].clear();
				mShadowEnvironmentDrawRecordsGpu[CascadeIndex].clear();
			}

			std::array<RenderContract::FrameGlobals, RenderContract::ShadowCascadeMaxCount> ShadowFrameGlobalsArray{};
			for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1) {
				RenderContract::FrameGlobals ShadowFrameGlobals{ Data.mFrameGlobals };
				ShadowFrameGlobals.mView = Data.mShadowMappingParameter.mShadowCameras[CascadeIndex].mView;
				ShadowFrameGlobals.mProj = Data.mShadowMappingParameter.mShadowCameras[CascadeIndex].mProj;
				ShadowFrameGlobals.mViewProj = Data.mShadowMappingParameter.mShadowCameras[CascadeIndex].mViewProj;
				ShadowFrameGlobals.mPrevViewProj = ShadowFrameGlobals.mViewProj;
				ShadowFrameGlobalsArray[CascadeIndex] = ShadowFrameGlobals;
			}

			RenderContract::FrameGlobals GpuFrameGlobals{ BuildGpuFrameGlobals(Data.mFrameGlobals) };
			std::array<RenderContract::FrameGlobals, RenderContract::ShadowCascadeMaxCount> GpuShadowFrameGlobalsArray{};
			for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1) {
				GpuShadowFrameGlobalsArray[CascadeIndex] = BuildGpuFrameGlobals(ShadowFrameGlobalsArray[CascadeIndex]);
			}

			RenderContract::ShadowMappingParameter GpuShadowMappingParameter{ BuildGpuShadowMappingParameter(Data.mShadowMappingParameter) };
			BuildGpuModelContexts(Data.mModelContexts, mGpuModelContexts);
			BuildGpuBonePalette(Data.mBonePalette, mGpuBonePalette);
			if (IsEnvironmentGpuDrivenFrameEnabled == false) {
				BuildGpuEnvironmentSegmentContexts(Data.mEnvironmentSegmentContexts, mGpuEnvironmentSegmentContexts);
			}
			else {
				mGpuEnvironmentSegmentContexts.clear();
			}

			for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1) {
				BuildGpuModelContexts(Data.mShadowRenderContexts[CascadeIndex].mModelContexts, mGpuShadowModelContexts[CascadeIndex]);
			}

			for (std::uint32_t CascadeIndex{ ShadowCascadeCount }; CascadeIndex < RenderContract::ShadowCascadeMaxCount; CascadeIndex += 1) {
				mGpuShadowModelContexts[CascadeIndex].clear();
			}

			std::vector<Interface::CopyRequest> CopyRequests{};
			CopyRequests.reserve(12ULL + (static_cast<std::size_t>(ShadowCascadeCount) * 4ULL));

			AddGraphicsVectorCopyRequest(mFrameGlobalsVector, GraphicsAllocator, MakeByteSpan(GpuFrameGlobals), CopyRequests, "Failed to prepare frame globals copy request.");
			AddGraphicsVectorCopyRequest(mShadowFrameGlobalsVector, GraphicsAllocator, MakeByteSpan(GpuShadowFrameGlobalsArray, static_cast<std::size_t>(ShadowCascadeCount)), CopyRequests, "Failed to prepare shadow frame globals copy request.");
			AddGraphicsVectorCopyRequest(mShadowMappingParameterVector, GraphicsAllocator, MakeByteSpan(GpuShadowMappingParameter), CopyRequests, "Failed to prepare shadow mapping parameter copy request.");
			AddGraphicsVectorCopyRequest(mModelContextVector, GraphicsAllocator, MakeByteSpan(mGpuModelContexts), CopyRequests, "Failed to prepare model context copy request.");
			AddGraphicsVectorCopyRequest(mBoundingBoxContextVector, GraphicsAllocator, MakeByteSpan(Data.mBoundingBoxContexts), CopyRequests, "Failed to prepare bounding box context copy request.");
			AddGraphicsVectorCopyRequest(mDebugGeometryContextVector, GraphicsAllocator, MakeByteSpan(Data.mDebugGeometryContexts), CopyRequests, "Failed to prepare debug geometry context copy request.");
			AddGraphicsVectorCopyRequest(mTerrainPatchContextVector, GraphicsAllocator, MakeByteSpan(Data.mTerrainPatchContexts), CopyRequests, "Failed to prepare terrain patch context copy request.");
			AddGraphicsVectorCopyRequest(mBonePaletteVector, GraphicsAllocator, MakeByteSpan(mGpuBonePalette), CopyRequests, "Failed to prepare bone palette copy request.");
			AddGraphicsVectorCopyRequest(mDrawRecordVector, GraphicsAllocator, MakeByteSpan(mDrawRecordsGpu), CopyRequests, "Failed to prepare draw record copy request.");
			if (IsEnvironmentGpuDrivenFrameEnabled == false) {
				AddGraphicsVectorCopyRequest(mEnvironmentInstanceContextVector, GraphicsAllocator, MakeByteSpan(Data.mEnvironmentInstanceContexts), CopyRequests, "Failed to prepare environment instance context copy request.");
				AddGraphicsVectorCopyRequest(mEnvironmentSegmentContextVector, GraphicsAllocator, MakeByteSpan(mGpuEnvironmentSegmentContexts), CopyRequests, "Failed to prepare environment segment context copy request.");
				AddGraphicsVectorCopyRequest(mEnvironmentDrawRecordVector, GraphicsAllocator, MakeByteSpan(mEnvironmentDrawRecordsGpu), CopyRequests, "Failed to prepare environment draw record copy request.");
			}

			for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1) {
				const RenderContract::ShadowRenderContext& ShadowRenderContext{ Data.mShadowRenderContexts[CascadeIndex] };
				AddGraphicsVectorCopyRequest(mShadowModelContextVectors[CascadeIndex], GraphicsAllocator, MakeByteSpan(mGpuShadowModelContexts[CascadeIndex]), CopyRequests, "Failed to prepare shadow model context copy request.");
				AddGraphicsVectorCopyRequest(mShadowTerrainPatchContextVectors[CascadeIndex], GraphicsAllocator, MakeByteSpan(ShadowRenderContext.mTerrainPatchContexts), CopyRequests, "Failed to prepare shadow terrain patch context copy request.");
				AddGraphicsVectorCopyRequest(mShadowDrawRecordVectors[CascadeIndex], GraphicsAllocator, MakeByteSpan(mShadowDrawRecordsGpu[CascadeIndex]), CopyRequests, "Failed to prepare shadow draw record copy request.");
				if (IsEnvironmentGpuDrivenFrameEnabled == false) {
					AddGraphicsVectorCopyRequest(mShadowEnvironmentDrawRecordVectors[CascadeIndex], GraphicsAllocator, MakeByteSpan(mShadowEnvironmentDrawRecordsGpu[CascadeIndex]), CopyRequests, "Failed to prepare shadow environment draw record copy request.");
				}
			}

			mCopyFuture = CopyQueue->EnqueueCopyFuture(CopyRequests);
			ErrorHandler::report(mCopyFuture.IsValid() == false, "DrawCallResourceManager", "Failed to enqueue frame upload copy requests.", ErrorHandler::Level::Critical);

			DrawCallResourceManager::UpdateShaderResourceViews(1, ShadowCascadeCount, 1, static_cast<std::uint32_t>(Data.mModelContexts.size()), static_cast<std::uint32_t>(Data.mBoundingBoxContexts.size()), static_cast<std::uint32_t>(Data.mDebugGeometryContexts.size()), static_cast<std::uint32_t>(Data.mTerrainPatchContexts.size()), static_cast<std::uint32_t>(Data.mBonePalette.size()), static_cast<std::uint32_t>(mDrawRecordsGpu.size()));
			DrawCallResourceManager::UpdateShadowShaderResourceViews(ShadowCascadeCount);
			if (IsEnvironmentGpuDrivenFrameEnabled == false) {
				DrawCallResourceManager::UpdateEnvironmentShaderResourceViews(static_cast<std::uint32_t>(Data.mEnvironmentInstanceContexts.size()), static_cast<std::uint32_t>(Data.mEnvironmentSegmentContexts.size()), static_cast<std::uint32_t>(mEnvironmentDrawRecordsGpu.size()));
				DrawCallResourceManager::UpdateShadowEnvironmentShaderResourceViews(ShadowCascadeCount);
			}
		}

		const RenderContract::Future& DrawCallResourceManager::GetCopyFuture() const {
			return mCopyFuture;
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

		DescriptorHandle DrawCallResourceManager::GetEnvironmentInstanceContextSrvHandle() const {
			return mEnvironmentInstanceContextSrvHandle;
		}

		DescriptorHandle DrawCallResourceManager::GetEnvironmentSegmentContextSrvHandle() const {
			return mEnvironmentSegmentContextSrvHandle;
		}

		DescriptorHandle DrawCallResourceManager::GetEnvironmentDrawRecordSrvHandle() const {
			return mEnvironmentDrawRecordSrvHandle;
		}

		DescriptorHandle DrawCallResourceManager::GetShadowModelContextSrvHandle(std::uint32_t ShadowCascadeIndex) const {
			const std::uint32_t ClampedShadowCascadeIndex{ std::min<std::uint32_t>(ShadowCascadeIndex, RenderContract::ShadowCascadeMaxCount - 1u) };
			return mShadowModelContextSrvHandles[ClampedShadowCascadeIndex];
		}

		DescriptorHandle DrawCallResourceManager::GetShadowTerrainPatchContextSrvHandle(std::uint32_t ShadowCascadeIndex) const {
			const std::uint32_t ClampedShadowCascadeIndex{ std::min<std::uint32_t>(ShadowCascadeIndex, RenderContract::ShadowCascadeMaxCount - 1u) };
			return mShadowTerrainPatchContextSrvHandles[ClampedShadowCascadeIndex];
		}

		DescriptorHandle DrawCallResourceManager::GetShadowDrawRecordSrvHandle(std::uint32_t ShadowCascadeIndex) const {
			const std::uint32_t ClampedShadowCascadeIndex{ std::min<std::uint32_t>(ShadowCascadeIndex, RenderContract::ShadowCascadeMaxCount - 1u) };
			return mShadowDrawRecordSrvHandles[ClampedShadowCascadeIndex];
		}

		DescriptorHandle DrawCallResourceManager::GetShadowEnvironmentDrawRecordSrvHandle(std::uint32_t ShadowCascadeIndex) const {
			const std::uint32_t ClampedShadowCascadeIndex{ std::min<std::uint32_t>(ShadowCascadeIndex, RenderContract::ShadowCascadeMaxCount - 1u) };
			return mShadowEnvironmentDrawRecordSrvHandles[ClampedShadowCascadeIndex];
		}

		bool DrawCallResourceManager::CompareDrawRecordByPso(const RenderContract::DrawRecord& Left, const RenderContract::DrawRecord& Right) {
			return std::tie(Left.mPass, Left.mPipeline, Left.mMesh, Left.mSubMesh) < std::tie(Right.mPass, Right.mPipeline, Right.mMesh, Right.mSubMesh);
		}

		bool DrawCallResourceManager::CompareEnvironmentDrawRecordByPso(const RenderContract::EnvironmentDrawRecord& Left, const RenderContract::EnvironmentDrawRecord& Right) {
			return std::tie(Left.mPass, Left.mPipeline, Left.mMesh, Left.mSubMesh, Left.mSegmentContextIndex, Left.mMaterialIndex, Left.mCastsShadow) < std::tie(Right.mPass, Right.mPipeline, Right.mMesh, Right.mSubMesh, Right.mSegmentContextIndex, Right.mMaterialIndex, Right.mCastsShadow);
		}

		void DrawCallResourceManager::SortShadowDrawRecords(std::vector<RenderContract::DrawRecord>& DrawRecords) {
			if (DrawRecords.size() < 2ULL) {
				return;
			}

			const bool IsSorted{ DrawCallResourceManager::IsDrawRecordsSorted(DrawRecords) };
			if (IsSorted == true) {
				return;
			}

			std::sort(DrawRecords.begin(), DrawRecords.end(), DrawCallResourceManager::CompareDrawRecordByPso);
		}

		void DrawCallResourceManager::SortShadowEnvironmentDrawRecords(std::vector<RenderContract::EnvironmentDrawRecord>& DrawRecords) {
			if (DrawRecords.size() < 2ULL) {
				return;
			}

			const bool IsSorted{ DrawCallResourceManager::IsEnvironmentDrawRecordsSorted(DrawRecords) };
			if (IsSorted == true) {
				return;
			}

			std::sort(DrawRecords.begin(), DrawRecords.end(), DrawCallResourceManager::CompareEnvironmentDrawRecordByPso);
		}

		bool DrawCallResourceManager::IsDrawRecordsSorted(const std::vector<RenderContract::DrawRecord>& DrawRecords) {
			return std::is_sorted(DrawRecords.begin(), DrawRecords.end(), DrawCallResourceManager::CompareDrawRecordByPso);
		}

		bool DrawCallResourceManager::IsEnvironmentDrawRecordsSorted(const std::vector<RenderContract::EnvironmentDrawRecord>& DrawRecords) {
			return std::is_sorted(DrawRecords.begin(), DrawRecords.end(), DrawCallResourceManager::CompareEnvironmentDrawRecordByPso);
		}

		void DrawCallResourceManager::BuildDrawRecordGpu(const std::vector<RenderContract::DrawRecord>& DrawRecords, std::vector<DrawRecordGPU>& OutDrawRecordsGpu) {
			OutDrawRecordsGpu.resize(DrawRecords.size());
			for (std::size_t Index{ 0 }; Index < DrawRecords.size(); ++Index) {
				const RenderContract::DrawRecord& SourceRecord{ DrawRecords[Index] };
				DrawRecordGPU& DestinationRecord{ OutDrawRecordsGpu[Index] };
				DestinationRecord.ObjectIndex = SourceRecord.mObjectIndex;
				DestinationRecord.MaterialIndex = SourceRecord.mMaterialIndex;
				DestinationRecord.Flags = SourceRecord.mFlags;
				DestinationRecord.TerrainPatchContextIndex = SourceRecord.mTerrainPatchContextIndex;
			}
		}

		void DrawCallResourceManager::BuildEnvironmentDrawRecordGpu(const std::vector<RenderContract::EnvironmentDrawRecord>& DrawRecords, std::vector<RenderContract::EnvironmentDrawRecordGpu>& OutDrawRecordsGpu) {
			OutDrawRecordsGpu.resize(DrawRecords.size());
			for (std::size_t Index{ 0 }; Index < DrawRecords.size(); ++Index) {
				const RenderContract::EnvironmentDrawRecord& SourceRecord{ DrawRecords[Index] };
				RenderContract::EnvironmentDrawRecordGpu& DestinationRecord{ OutDrawRecordsGpu[Index] };
				DestinationRecord.mInstanceOffset = SourceRecord.mInstanceOffset;
				DestinationRecord.mInstanceCount = SourceRecord.mInstanceCount;
				DestinationRecord.mSegmentContextIndex = SourceRecord.mSegmentContextIndex;
				DestinationRecord.mMaterialIndex = SourceRecord.mMaterialIndex;
				DestinationRecord.mFlags = SourceRecord.mFlags;
				DestinationRecord.mVisibleInstanceOffset = 0u;
				DestinationRecord.mGpuDrivenFlags = 0u;
				DestinationRecord.mPadding2 = 0u;
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
					mFrameGlobalsVector.CreateShaderResourceView(mDevice, mFrameGlobalsSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, FrameGlobalsCount, sizeof(RenderContract::FrameGlobals), D3D12_BUFFER_SRV_FLAG_NONE);
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
					mShadowFrameGlobalsVector.CreateShaderResourceView(mDevice, mShadowFrameGlobalsSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, ShadowFrameGlobalsCount, sizeof(RenderContract::FrameGlobals), D3D12_BUFFER_SRV_FLAG_NONE);
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
					mShadowMappingParameterVector.CreateShaderResourceView(mDevice, mShadowMappingParameterSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, ShadowMappingParameterCount, sizeof(RenderContract::ShadowMappingParameter), D3D12_BUFFER_SRV_FLAG_NONE);
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
					mModelContextVector.CreateShaderResourceView(mDevice, mModelContextSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, ModelContextCount, sizeof(RenderContract::ModelContext), D3D12_BUFFER_SRV_FLAG_NONE);
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
					mBoundingBoxContextVector.CreateShaderResourceView(mDevice, mBoundingBoxContextSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, BoundingBoxContextCount, sizeof(RenderContract::BoundingBoxContext), D3D12_BUFFER_SRV_FLAG_NONE);
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
					mDebugGeometryContextVector.CreateShaderResourceView(mDevice, mDebugGeometryContextSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, DebugGeometryContextCount, sizeof(RenderContract::DebugGeometryContext), D3D12_BUFFER_SRV_FLAG_NONE);
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
					mTerrainPatchContextVector.CreateShaderResourceView(mDevice, mTerrainPatchContextSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, TerrainPatchContextCount, sizeof(RenderContract::TerrainPatchContext), D3D12_BUFFER_SRV_FLAG_NONE);
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

		void DrawCallResourceManager::UpdateEnvironmentShaderResourceViews(std::uint32_t EnvironmentInstanceContextCount, std::uint32_t EnvironmentSegmentContextCount, std::uint32_t EnvironmentDrawRecordCount) {
			ID3D12Resource* EnvironmentInstanceContextResource{ mEnvironmentInstanceContextVector.IsValid() == true ? mEnvironmentInstanceContextVector.GetResource() : nullptr };
			ID3D12Resource* EnvironmentSegmentContextResource{ mEnvironmentSegmentContextVector.IsValid() == true ? mEnvironmentSegmentContextVector.GetResource() : nullptr };
			ID3D12Resource* EnvironmentDrawRecordResource{ mEnvironmentDrawRecordVector.IsValid() == true ? mEnvironmentDrawRecordVector.GetResource() : nullptr };

			if (mEnvironmentInstanceContextVector.IsValid() == true) {
				bool IsUpdateRequired{ DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mEnvironmentInstanceContextSrvResource, EnvironmentInstanceContextResource, mEnvironmentInstanceContextSrvElementCount, EnvironmentInstanceContextCount) };
				if (IsUpdateRequired == true) {
					mEnvironmentInstanceContextVector.CreateShaderResourceView(mDevice, mEnvironmentInstanceContextSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, EnvironmentInstanceContextCount, sizeof(RenderContract::EnvironmentInstanceContext), D3D12_BUFFER_SRV_FLAG_NONE);
					mEnvironmentInstanceContextSrvResource = EnvironmentInstanceContextResource;
					mEnvironmentInstanceContextSrvElementCount = EnvironmentInstanceContextCount;
				}
			}
			else {
				mEnvironmentInstanceContextSrvResource = nullptr;
				mEnvironmentInstanceContextSrvElementCount = 0;
			}

			if (mEnvironmentSegmentContextVector.IsValid() == true) {
				bool IsUpdateRequired{ DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mEnvironmentSegmentContextSrvResource, EnvironmentSegmentContextResource, mEnvironmentSegmentContextSrvElementCount, EnvironmentSegmentContextCount) };
				if (IsUpdateRequired == true) {
					mEnvironmentSegmentContextVector.CreateShaderResourceView(mDevice, mEnvironmentSegmentContextSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, EnvironmentSegmentContextCount, sizeof(RenderContract::EnvironmentSegmentContext), D3D12_BUFFER_SRV_FLAG_NONE);
					mEnvironmentSegmentContextSrvResource = EnvironmentSegmentContextResource;
					mEnvironmentSegmentContextSrvElementCount = EnvironmentSegmentContextCount;
				}
			}
			else {
				mEnvironmentSegmentContextSrvResource = nullptr;
				mEnvironmentSegmentContextSrvElementCount = 0;
			}

			if (mEnvironmentDrawRecordVector.IsValid() == true) {
				bool IsUpdateRequired{ DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mEnvironmentDrawRecordSrvResource, EnvironmentDrawRecordResource, mEnvironmentDrawRecordSrvElementCount, EnvironmentDrawRecordCount) };
				if (IsUpdateRequired == true) {
					mEnvironmentDrawRecordVector.CreateShaderResourceView(mDevice, mEnvironmentDrawRecordSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, EnvironmentDrawRecordCount, sizeof(RenderContract::EnvironmentDrawRecordGpu), D3D12_BUFFER_SRV_FLAG_NONE);
					mEnvironmentDrawRecordSrvResource = EnvironmentDrawRecordResource;
					mEnvironmentDrawRecordSrvElementCount = EnvironmentDrawRecordCount;
				}
			}
			else {
				mEnvironmentDrawRecordSrvResource = nullptr;
				mEnvironmentDrawRecordSrvElementCount = 0;
			}
		}

		void DrawCallResourceManager::UpdateShadowShaderResourceViews(std::uint32_t ShadowCascadeCount) {
			for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < RenderContract::ShadowCascadeMaxCount; CascadeIndex += 1) {
				GraphicsVector& ShadowModelContextVector{ mShadowModelContextVectors[CascadeIndex] };
				GraphicsVector& ShadowTerrainPatchContextVector{ mShadowTerrainPatchContextVectors[CascadeIndex] };
				GraphicsVector& ShadowDrawRecordVector{ mShadowDrawRecordVectors[CascadeIndex] };
				const bool IsActiveCascade{ CascadeIndex < ShadowCascadeCount };
				ID3D12Resource* ShadowModelContextResource{ ShadowModelContextVector.IsValid() == true ? ShadowModelContextVector.GetResource() : nullptr };
				ID3D12Resource* ShadowTerrainPatchContextResource{ ShadowTerrainPatchContextVector.IsValid() == true ? ShadowTerrainPatchContextVector.GetResource() : nullptr };
				ID3D12Resource* ShadowDrawRecordResource{ ShadowDrawRecordVector.IsValid() == true ? ShadowDrawRecordVector.GetResource() : nullptr };
				const std::uint32_t ShadowModelContextCount{ IsActiveCascade == true ? static_cast<std::uint32_t>(ShadowModelContextVector.GetSizeInBytes() / sizeof(RenderContract::ModelContext)) : 0u };
				const std::uint32_t ShadowTerrainPatchContextCount{ IsActiveCascade == true ? static_cast<std::uint32_t>(ShadowTerrainPatchContextVector.GetSizeInBytes() / sizeof(RenderContract::TerrainPatchContext)) : 0u };
				const std::uint32_t ShadowDrawRecordCount{ IsActiveCascade == true ? static_cast<std::uint32_t>(ShadowDrawRecordVector.GetSizeInBytes() / sizeof(DrawRecordGPU)) : 0u };

				if (IsActiveCascade == true && ShadowModelContextVector.IsValid() == true) {
					bool IsUpdateRequired{ DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mShadowModelContextSrvResources[CascadeIndex], ShadowModelContextResource, mShadowModelContextSrvElementCounts[CascadeIndex], ShadowModelContextCount) };
					if (IsUpdateRequired == true) {
						ShadowModelContextVector.CreateShaderResourceView(mDevice, mShadowModelContextSrvHandles[CascadeIndex].GetCPU(), DXGI_FORMAT_UNKNOWN, 0, ShadowModelContextCount, sizeof(RenderContract::ModelContext), D3D12_BUFFER_SRV_FLAG_NONE);
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
						ShadowTerrainPatchContextVector.CreateShaderResourceView(mDevice, mShadowTerrainPatchContextSrvHandles[CascadeIndex].GetCPU(), DXGI_FORMAT_UNKNOWN, 0, ShadowTerrainPatchContextCount, sizeof(RenderContract::TerrainPatchContext), D3D12_BUFFER_SRV_FLAG_NONE);
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

		void DrawCallResourceManager::UpdateShadowEnvironmentShaderResourceViews(std::uint32_t ShadowCascadeCount) {
			for (std::uint32_t CascadeIndex{ 0 }; CascadeIndex < RenderContract::ShadowCascadeMaxCount; CascadeIndex += 1) {
				GraphicsVector& ShadowEnvironmentDrawRecordVector{ mShadowEnvironmentDrawRecordVectors[CascadeIndex] };
				const bool IsActiveCascade{ CascadeIndex < ShadowCascadeCount };
				ID3D12Resource* ShadowEnvironmentDrawRecordResource{ ShadowEnvironmentDrawRecordVector.IsValid() == true ? ShadowEnvironmentDrawRecordVector.GetResource() : nullptr };
				const std::uint32_t ShadowEnvironmentDrawRecordCount{ IsActiveCascade == true ? static_cast<std::uint32_t>(ShadowEnvironmentDrawRecordVector.GetSizeInBytes() / sizeof(RenderContract::EnvironmentDrawRecordGpu)) : 0u };

				if (IsActiveCascade == true && ShadowEnvironmentDrawRecordVector.IsValid() == true) {
					bool IsUpdateRequired{ DrawCallResourceManager::IsShaderResourceViewUpdateRequired(mShadowEnvironmentDrawRecordSrvResources[CascadeIndex], ShadowEnvironmentDrawRecordResource, mShadowEnvironmentDrawRecordSrvElementCounts[CascadeIndex], ShadowEnvironmentDrawRecordCount) };
					if (IsUpdateRequired == true) {
						ShadowEnvironmentDrawRecordVector.CreateShaderResourceView(mDevice, mShadowEnvironmentDrawRecordSrvHandles[CascadeIndex].GetCPU(), DXGI_FORMAT_UNKNOWN, 0, ShadowEnvironmentDrawRecordCount, sizeof(RenderContract::EnvironmentDrawRecordGpu), D3D12_BUFFER_SRV_FLAG_NONE);
						mShadowEnvironmentDrawRecordSrvResources[CascadeIndex] = ShadowEnvironmentDrawRecordResource;
						mShadowEnvironmentDrawRecordSrvElementCounts[CascadeIndex] = ShadowEnvironmentDrawRecordCount;
					}
				}
				else {
					mShadowEnvironmentDrawRecordSrvResources[CascadeIndex] = nullptr;
					mShadowEnvironmentDrawRecordSrvElementCounts[CascadeIndex] = 0;
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
