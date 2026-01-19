#include "GraphicsBuffer.h"
#include <iomanip>
#include <sstream>
#include "External/Include/DirectXTK12/d3dx12.h"

#ifdef min 
#undef min 
#endif 

namespace Core {
    namespace DX {
        GraphicsResource::GraphicsResource() {}

        GraphicsResource::GraphicsResource(ID3D12Device* device, UINT64 capacity, D3D12_RESOURCE_FLAGS flags) {

            auto defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto defaultBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(capacity,flags);
			auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(capacity);

            if (FAILED(device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &defaultBufferDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mDefaultHeap)))) {
                ErrorHandler::report("GraphicsBuffer", "Failed to create GPU resource", ErrorHandler::Level::Critical);
            }
            mDefaultHeapState = D3D12_RESOURCE_STATE_COMMON;

            if (FAILED(device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mUploadHeap)))) {
                ErrorHandler::report("GraphicsBuffer", "Failed to create Upload resource", ErrorHandler::Level::Critical);
            }

            mUploadHeap->Map(0, nullptr, reinterpret_cast<void**>(&mMappedPtr));
        }

        GraphicsResource::~GraphicsResource() {
            Unmap(); 
        }

        GraphicsResource::GraphicsResource(GraphicsResource&& other) noexcept : mDefaultHeap(std::move(other.mDefaultHeap)),
			                                                                    mUploadHeap(std::move(other.mUploadHeap)),
			                                                                    mMappedPtr(other.mMappedPtr),
			                                                                    mCapacity(other.mCapacity),
			                                                                    mDefaultHeapState(other.mDefaultHeapState) {
			other.mMappedPtr = nullptr;
			other.mCapacity = 0;
        }
        
        GraphicsResource& GraphicsResource::operator=(GraphicsResource&& other) noexcept {
            if (this != &other) {
                if (mUploadHeap && mMappedPtr) {
                    mUploadHeap->Unmap(0, nullptr);
                }
                mDefaultHeap = std::move(other.mDefaultHeap);
                mUploadHeap = std::move(other.mUploadHeap);
                mMappedPtr = other.mMappedPtr;
                mCapacity = other.mCapacity;
                mDefaultHeapState = other.mDefaultHeapState;

                other.mMappedPtr = nullptr;
                other.mCapacity = 0;
            }

            return *this;
        }

        void GraphicsResource::CopyToGPU(ID3D12GraphicsCommandList* cmdList) {
            if (not mDefaultHeap or not mUploadHeap) {
				ErrorHandler::report("GraphicsResource", "Invalid resources for CopyToGPU", ErrorHandler::Level::Critical);
                return;
            }

            Transition(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
            cmdList->CopyResource(mDefaultHeap.Get(), mUploadHeap.Get());
            Transition(cmdList, D3D12_RESOURCE_STATE_GENERIC_READ);
        }

        void GraphicsResource::Transition(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES newState) {
            if (mDefaultHeapState == newState) {
                return;
            }

            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mDefaultHeap.Get(), mDefaultHeapState, newState);

            cmdList->ResourceBarrier(1, &barrier);
            mDefaultHeapState = newState;
        }

        void GraphicsResource::Write(const void* data, UINT64 size, UINT64 offset) {
            if (mMappedPtr && data && (offset + size <= mCapacity)) {
                memcpy(mMappedPtr + offset, data, size);
            }
        }

        ID3D12Resource* GraphicsResource::GetDefault() const {
            return mDefaultHeap.Get();
        }

        D3D12_GPU_VIRTUAL_ADDRESS GraphicsResource::GetGPUAddress() const {
            return mDefaultHeap->GetGPUVirtualAddress();
        }

        D3D12_RESOURCE_STATES GraphicsResource::GetCurrentState() const {
			return mDefaultHeapState;
        }


        void GraphicsResource::Unmap() {
			if (mUploadHeap and mMappedPtr) {
				mUploadHeap->Unmap(0, nullptr);
				mMappedPtr = nullptr;
			}
        }


        GraphicsBuffer::GraphicsBuffer() : GraphicsResource(), mIsFinalized(false) {
        
        }

        GraphicsBuffer::GraphicsBuffer(ID3D12Device* device, UINT64 capacity) : GraphicsResource(device, capacity), mIsFinalized(false) {

        }

        GraphicsBuffer::~GraphicsBuffer() {
             
        }

        GraphicsBuffer::GraphicsBuffer(GraphicsBuffer&& other) noexcept :   GraphicsResource(std::move(other)),
                                                                            mIsFinalized(other.mIsFinalized),
                                                                            mRegisteredTypes(std::move(other.mRegisteredTypes)),
                                                                            mSubResources(std::move(other.mSubResources)) {
            other.mMappedPtr = nullptr;
            other.mCapacity = 0;
            other.mIsFinalized = false;

        }

        GraphicsBuffer& GraphicsBuffer::operator=(GraphicsBuffer&& other) noexcept {
            if (this != &other) {
                
				GraphicsResource::operator=(std::move(other));

                mIsFinalized = other.mIsFinalized;
                mRegisteredTypes = std::move(other.mRegisteredTypes);
                mSubResources = std::move(other.mSubResources);

                other.mIsFinalized = false;
            }
            return *this;
        }

        void GraphicsBuffer::Finalize() {
            if (!mDefaultHeap or mRegisteredTypes.empty()) {
                ErrorHandler::report("GraphicsBuffer", "Invalid state for Finalize", ErrorHandler::Level::Critical);
            }
            if (mIsFinalized) {
                return;
            }

            UINT64 sizePerType = mCapacity / mRegisteredTypes.size();
            UINT64 currentOffset = 0;

            for (const auto& entry : mRegisteredTypes) {
                currentOffset = (currentOffset + 15) & ~15; // 16-byte alignment

                SubResourceInfo info;
                info.baseOffset = currentOffset;
                info.stride = static_cast<UINT>(entry.stride);

                UINT64 availableSpace = sizePerType;
                if (currentOffset + availableSpace > mCapacity) {
                    availableSpace = mCapacity - currentOffset;
                }

                info.maxElementCount = static_cast<UINT>(availableSpace / info.stride);
                info.currentElementCursor = 0;

                mSubResources[entry.key] = info;
                currentOffset += availableSpace;
            }

            mIsFinalized = true;
        }

        void GraphicsBuffer::CopyDirtyRegionsToGPU(ID3D12GraphicsCommandList* cmdList) {
			GraphicsBuffer::Transition(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);

            for (auto& [key, info] : mSubResources) {
                if (info.currentElementCursor > 0) {
                    UINT64 dataSize = static_cast<UINT64>(info.currentElementCursor) * info.stride;
                    cmdList->CopyBufferRegion(mDefaultHeap.Get(), info.baseOffset, mUploadHeap.Get(), info.baseOffset, dataSize);
                }
            }

			GraphicsBuffer::Transition(cmdList, D3D12_RESOURCE_STATE_GENERIC_READ);
        }

        void GraphicsBuffer::Reset() {
            for (auto& pair : mSubResources) {
                pair.second.currentElementCursor = 0;
            }
        }

#if defined(DEBUG) || defined(_DEBUG)
        std::string GraphicsBuffer::GetBufferStatusString() const {
            if (!mIsFinalized) {
                return "GraphicsBuffer: Not Finalized yet.";
            }

            std::ostringstream oss;
            oss << "\n" << std::string(70, '=') << "\n";
            oss << " [GraphicsBuffer Debug Status]\n";
            oss << " Total Capacity: " << (mCapacity / (1024.0 * 1024.0)) << " MB\n";
            oss << " Current State: " << static_cast<int>(mDefaultHeapState) << "\n";
            oss << std::string(70, '-') << "\n";


            oss << std::left << std::setw(25) << "Resource Name"
                << std::setw(15) << "Offset"
                << std::setw(18) << "Usage (Count)"
                << "Stride\n";
            oss << std::string(70, '-') << "\n";

            for (const auto& entry : mRegisteredTypes) {
                auto it = mSubResources.find(entry.key);
                if (it == mSubResources.end()) {
                    continue;
                }

                const auto& info = it->second;
                std::string usage = std::to_string(info.currentElementCursor) + " / " + std::to_string(info.maxElementCount);

                oss << std::left << std::setw(25) << entry.debugName
                    << "0x" << std::hex << std::setw(13) << std::setfill('0') << info.baseOffset << std::dec << std::setfill(' ')
                    << std::setw(18) << usage
                    << entry.stride << " bytes\n";
            }

            oss << std::string(70, '=') << "\n";

            return oss.str();
        }
#endif 

        GraphicsBuffer::SubResourceInfo& GraphicsBuffer::GetSubResourceInfo(ResourceKey key) {
            if (not mIsFinalized) {
                ErrorHandler::report("GraphicsBuffer", "Not Finalized", ErrorHandler::Level::Critical);
            }
            auto it = mSubResources.find(key);
            if (it == mSubResources.end()) {
                ErrorHandler::report("GraphicsBuffer", "Key Not Found", ErrorHandler::Level::Critical);
            }
            return it->second;
        }

        const GraphicsBuffer::SubResourceInfo& GraphicsBuffer::GetSubResourceInfo(ResourceKey key) const {
            if (not mIsFinalized) {
                ErrorHandler::report("GraphicsBuffer", "Not Finalized", ErrorHandler::Level::Critical);
            }
            auto it = mSubResources.find(key);
            if (it == mSubResources.end()) {
                ErrorHandler::report("GraphicsBuffer", "Key Not Found", ErrorHandler::Level::Critical);
            }
            return it->second;
        }
    }
}