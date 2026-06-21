#include "MaterialResourceManager.h"
#include <span>
#include <vector>
#include "Core/DX/GraphicsAllocator.h"
#include "Utility/ErrorHandler.h"

namespace Core {
	namespace DX {
		namespace {
			template <typename T>
			std::span<const std::byte> MakeByteSpan(const std::vector<T>& Values) {
				return std::as_bytes(std::span<const T>{ Values.data(), Values.size() });
			}
		}

		MaterialResourceManager::MaterialResourceManager() {
		}

		MaterialResourceManager::~MaterialResourceManager() {
		}

		void MaterialResourceManager::Initialize(ID3D12Device* Device, DescriptorHeap* SrvHeap) {
			mDevice = Device;
			mSrvHeap = SrvHeap;

			mMaterialSrvHandle = mSrvHeap->Allocate();
			for (std::size_t Index{ 0 }; Index < Constants::FrameCount<std::size_t>; ++Index) {
				mMaterialTextureTableSrvHandles[Index] = mSrvHeap->Allocate();
				mPerFrameCopyFutures[Index] = RenderContract::Future{};
				mPerFrameMaterialTextureTableHashes[Index] = 0;
				mPerFrameMaterialTextureTableSizesInBytes[Index] = 0;
			}
		}

		void MaterialResourceManager::PrepareFrameResources(std::uint32_t RtvIndex, const RenderContract::RenderFrameData& Data, GraphicsAllocator& GraphicsAllocator, Interface::ICopyQueue* CopyQueue) {
			GraphicsVector& MaterialTextureTableVector{ mPerFrameMaterialTextureTableVectors[RtvIndex] };
			std::size_t MaterialSizeInBytes{ sizeof(RenderContract::MaterialGpu) * Data.mMaterials.size() };
			std::size_t MaterialTextureTableSizeInBytes{ sizeof(RenderContract::MaterialTextureTableItemGpu) * Data.mMaterialTextureTable.size() };
			std::span<const std::byte> MaterialSourceData{ MakeByteSpan(Data.mMaterials) };
			std::span<const std::byte> MaterialTextureTableSourceData{ MakeByteSpan(Data.mMaterialTextureTable) };

			std::uint64_t CurrentMaterialHash{ MaterialResourceManager::ComputeDataHash(MaterialSourceData.data(), MaterialSizeInBytes) };
			bool IsMaterialUploadRequired{ mMaterialVector.IsValid() == false || mMaterialSizeInBytes != MaterialSizeInBytes || mMaterialHash != CurrentMaterialHash };
			std::uint64_t CurrentMaterialTextureTableHash{ MaterialResourceManager::ComputeDataHash(MaterialTextureTableSourceData.data(), MaterialTextureTableSizeInBytes) };
			bool IsMaterialTextureTableUploadRequired{ MaterialTextureTableVector.IsValid() == false || mPerFrameMaterialTextureTableSizesInBytes[RtvIndex] != MaterialTextureTableSizeInBytes || mPerFrameMaterialTextureTableHashes[RtvIndex] != CurrentMaterialTextureTableHash };

			std::vector<Interface::CopyRequest> CopyRequests{};
			CopyRequests.reserve(2);
			if (IsMaterialUploadRequired == true) {
				Interface::CopyRequest MaterialCopyRequest{ Interface::CopyPriority::High };
				bool MaterialCopyResult{ mMaterialVector.PrepareCopyRequest(GraphicsAllocator, MaterialSourceData, Interface::CopyPriority::High, MaterialCopyRequest, 0) };
				ErrorHandler::report(MaterialCopyResult == false, "MaterialResourceManager", "Failed to prepare material copy request.", ErrorHandler::Level::Critical);
				if (MaterialCopyResult == true and MaterialCopyRequest.SourceData.empty() == false and MaterialCopyRequest.DestinationDefaultResource != nullptr) {
					CopyRequests.push_back(MaterialCopyRequest);
				}

				mMaterialHash = CurrentMaterialHash;
				mMaterialSizeInBytes = MaterialSizeInBytes;
			}

			if (IsMaterialTextureTableUploadRequired == true) {
				Interface::CopyRequest MaterialTextureTableCopyRequest{ Interface::CopyPriority::High };
				bool MaterialTextureTableCopyResult{ MaterialTextureTableVector.PrepareCopyRequest(GraphicsAllocator, MaterialTextureTableSourceData, Interface::CopyPriority::High, MaterialTextureTableCopyRequest, 0) };
				ErrorHandler::report(MaterialTextureTableCopyResult == false, "MaterialResourceManager", "Failed to prepare material texture table copy request.", ErrorHandler::Level::Critical);
				if (MaterialTextureTableCopyResult == true and MaterialTextureTableCopyRequest.SourceData.empty() == false and MaterialTextureTableCopyRequest.DestinationDefaultResource != nullptr) {
					CopyRequests.push_back(MaterialTextureTableCopyRequest);
				}

				mPerFrameMaterialTextureTableHashes[RtvIndex] = CurrentMaterialTextureTableHash;
				mPerFrameMaterialTextureTableSizesInBytes[RtvIndex] = MaterialTextureTableSizeInBytes;
			}

			if (CopyRequests.empty() == false) {
				mPerFrameCopyFutures[RtvIndex] = CopyQueue->EnqueueCopyFuture(CopyRequests);
				ErrorHandler::report(mPerFrameCopyFutures[RtvIndex].IsValid() == false, "MaterialResourceManager", "Failed to enqueue material upload copy requests.", ErrorHandler::Level::Critical);
			}
			else {
				mPerFrameCopyFutures[RtvIndex] = RenderContract::Future{};
			}

			MaterialResourceManager::UpdateMaterialShaderResourceView(static_cast<std::uint32_t>(Data.mMaterials.size()));
			MaterialResourceManager::UpdateMaterialTextureTableShaderResourceView(RtvIndex, static_cast<std::uint32_t>(Data.mMaterialTextureTable.size()));
		}

		const RenderContract::Future& MaterialResourceManager::GetCopyFuture(std::uint32_t RtvIndex) const {
			return mPerFrameCopyFutures[RtvIndex];
		}

		DescriptorHandle MaterialResourceManager::GetMaterialSrvHandle() const {
			return mMaterialSrvHandle;
		}

		DescriptorHandle MaterialResourceManager::GetMaterialTextureTableSrvHandle(std::uint32_t RtvIndex) const {
			return mMaterialTextureTableSrvHandles[RtvIndex];
		}

		std::uint64_t MaterialResourceManager::ComputeDataHash(const void* Data, std::size_t SizeInBytes) {
			const std::uint64_t OffsetBasis{ 1469598103934665603ull };
			const std::uint64_t Prime{ 1099511628211ull };
			const std::byte* Bytes{ static_cast<const std::byte*>(Data) };
			std::uint64_t HashValue{ OffsetBasis };

			for (std::size_t Index{ 0 }; Index < SizeInBytes; ++Index) {
				HashValue ^= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(Bytes[Index]));
				HashValue *= Prime;
			}

			HashValue ^= static_cast<std::uint64_t>(SizeInBytes);
			HashValue *= Prime;

			return HashValue;
		}

		void MaterialResourceManager::UpdateMaterialShaderResourceView(std::uint32_t MaterialCount) {
			ID3D12Resource* MaterialResource{ mMaterialVector.IsValid() == true ? mMaterialVector.GetResource() : nullptr };
			if (mMaterialVector.IsValid() == true) {
				bool IsUpdateRequired{ MaterialResourceManager::IsShaderResourceViewUpdateRequired(mMaterialSrvResource, MaterialResource, mMaterialSrvElementCount, MaterialCount) };
				if (IsUpdateRequired == true) {
					mMaterialVector.CreateShaderResourceView(mDevice, mMaterialSrvHandle.GetCPU(), DXGI_FORMAT_UNKNOWN, 0, MaterialCount, sizeof(RenderContract::MaterialGpu), D3D12_BUFFER_SRV_FLAG_NONE);
					mMaterialSrvResource = MaterialResource;
					mMaterialSrvElementCount = MaterialCount;
				}
			}
			else {
				mMaterialSrvResource = nullptr;
				mMaterialSrvElementCount = 0;
			}
		}

		void MaterialResourceManager::UpdateMaterialTextureTableShaderResourceView(std::uint32_t RtvIndex, std::uint32_t MaterialTextureTableCount) {
			GraphicsVector& MaterialTextureTableVector{ mPerFrameMaterialTextureTableVectors[RtvIndex] };
			ID3D12Resource* MaterialTextureTableResource{ MaterialTextureTableVector.IsValid() == true ? MaterialTextureTableVector.GetResource() : nullptr };
			if (MaterialTextureTableVector.IsValid() == true) {
				bool IsUpdateRequired{ MaterialResourceManager::IsShaderResourceViewUpdateRequired(mMaterialTextureTableSrvResources[RtvIndex], MaterialTextureTableResource, mMaterialTextureTableSrvElementCounts[RtvIndex], MaterialTextureTableCount) };
				if (IsUpdateRequired == true) {
					MaterialTextureTableVector.CreateShaderResourceView(mDevice, mMaterialTextureTableSrvHandles[RtvIndex].GetCPU(), DXGI_FORMAT_UNKNOWN, 0, MaterialTextureTableCount, sizeof(RenderContract::MaterialTextureTableItemGpu), D3D12_BUFFER_SRV_FLAG_NONE);
					mMaterialTextureTableSrvResources[RtvIndex] = MaterialTextureTableResource;
					mMaterialTextureTableSrvElementCounts[RtvIndex] = MaterialTextureTableCount;
				}
			}
			else {
				mMaterialTextureTableSrvResources[RtvIndex] = nullptr;
				mMaterialTextureTableSrvElementCounts[RtvIndex] = 0;
			}
		}

		bool MaterialResourceManager::IsShaderResourceViewUpdateRequired(ID3D12Resource* CachedResource, ID3D12Resource* CurrentResource, std::uint32_t CachedElementCount, std::uint32_t CurrentElementCount) const {
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
