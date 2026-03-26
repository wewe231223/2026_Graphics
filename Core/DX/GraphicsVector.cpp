#include "GraphicsVector.h"
#include <algorithm>
#include <cstring>

using namespace Core::DX;

GraphicsVector::GraphicsVector()
    : mAllocationHandle{},
    mUploadData{},
    mSizeInBytes{},
    mCapacityInBytes{},
    mResourceFlags{ D3D12_RESOURCE_FLAG_NONE },
    mResourceState{ D3D12_RESOURCE_STATE_COMMON },
    mCopyRequestCreationCount{} {
}


GraphicsVector::GraphicsVector(GraphicsAllocator& graphicsAllocator, SizeType initialSizeInBytes, D3D12_RESOURCE_FLAGS resourceFlags, D3D12_RESOURCE_STATES initialState)
    : GraphicsVector{} {
    Initialize(graphicsAllocator, initialSizeInBytes, resourceFlags, initialState);
}

GraphicsVector::~GraphicsVector() {
    Reset();
}

GraphicsVector::GraphicsVector(GraphicsVector&& other) noexcept
    : mAllocationHandle{ std::move(other.mAllocationHandle) },
    mUploadData{ std::move(other.mUploadData) },
    mSizeInBytes{ other.mSizeInBytes },
    mCapacityInBytes{ other.mCapacityInBytes },
    mResourceFlags{ other.mResourceFlags },
    mResourceState{ other.mResourceState },
    mCopyRequestCreationCount{ other.mCopyRequestCreationCount } {
    other.mSizeInBytes = 0;
    other.mCapacityInBytes = 0;
    other.mResourceFlags = D3D12_RESOURCE_FLAG_NONE;
    other.mResourceState = D3D12_RESOURCE_STATE_COMMON;
    other.mCopyRequestCreationCount = 0;
}

GraphicsVector& GraphicsVector::operator=(GraphicsVector&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    Reset();
    mAllocationHandle = std::move(other.mAllocationHandle);
    mUploadData = std::move(other.mUploadData);
    mSizeInBytes = other.mSizeInBytes;
    mCapacityInBytes = other.mCapacityInBytes;
    mResourceFlags = other.mResourceFlags;
    mResourceState = other.mResourceState;
    mCopyRequestCreationCount = other.mCopyRequestCreationCount;

    other.mSizeInBytes = 0;
    other.mCapacityInBytes = 0;
    other.mResourceFlags = D3D12_RESOURCE_FLAG_NONE;
    other.mResourceState = D3D12_RESOURCE_STATE_COMMON;
    other.mCopyRequestCreationCount = 0;
    return *this;
}

bool GraphicsVector::Initialize(GraphicsAllocator& graphicsAllocator, SizeType initialSizeInBytes, D3D12_RESOURCE_FLAGS resourceFlags, D3D12_RESOURCE_STATES initialState) {
    Reset();

    mResourceFlags = resourceFlags;
    mResourceState = initialState;
    mUploadData.clear();
    mUploadData.resize(initialSizeInBytes);
    mSizeInBytes = initialSizeInBytes;
    mCopyRequestCreationCount = 0;

    if (initialSizeInBytes == 0) {
        mCapacityInBytes = 0;
        return true;
    }

    return Reallocate(graphicsAllocator, initialSizeInBytes);
}

bool GraphicsVector::Copy(GraphicsAllocator& graphicsAllocator, void* sourceData, SizeType copySizeInBytes) {
    if (sourceData == nullptr) {
        return false;
    }

    if (copySizeInBytes > mCapacityInBytes) {
        bool reallocateResult{ Reallocate(graphicsAllocator, copySizeInBytes) };
        if (reallocateResult == false) {
            return false;
        }
    }

    mUploadData.resize(copySizeInBytes);
    std::memcpy(mUploadData.data(), sourceData, copySizeInBytes);
    mSizeInBytes = copySizeInBytes;
    return true;
}

void GraphicsVector::Reset() {
    mAllocationHandle.Reset();
    mUploadData.clear();
    mSizeInBytes = 0;
    mCapacityInBytes = 0;
    mResourceFlags = D3D12_RESOURCE_FLAG_NONE;
    mResourceState = D3D12_RESOURCE_STATE_COMMON;
    mCopyRequestCreationCount = 0;
}

Interface::CopyQueueCopyRequest GraphicsVector::CreateCopyQueueCopyRequest(GraphicsAllocator& graphicsAllocator, UINT64 destinationOffset) {
    Interface::CopyQueueCopyRequest copyQueueCopyRequest{};
    copyQueueCopyRequest.DestinationDefaultResource = mAllocationHandle.GetResourceComPtr();
    copyQueueCopyRequest.DestinationOffset = destinationOffset;
    copyQueueCopyRequest.SourceData.resize(mSizeInBytes);

    if (mSizeInBytes > 0) {
        std::copy(mUploadData.cbegin(), mUploadData.cbegin() + static_cast<std::ptrdiff_t>(mSizeInBytes), copyQueueCopyRequest.SourceData.begin());
    }

    mCopyRequestCreationCount += 1;
    TryShrink(graphicsAllocator);
    return copyQueueCopyRequest;
}

void GraphicsVector::CreateShaderResourceView(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle, DXGI_FORMAT format, UINT firstElement, UINT numElements, UINT structureByteStride, D3D12_BUFFER_SRV_FLAGS bufferFlags) const {
    if (device == nullptr || mAllocationHandle.IsValid() == false) {
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDescription{};
    shaderResourceViewDescription.Format = format;
    shaderResourceViewDescription.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    shaderResourceViewDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    shaderResourceViewDescription.Buffer.FirstElement = firstElement;
    shaderResourceViewDescription.Buffer.NumElements = numElements == 0 ? 1 : numElements;
    shaderResourceViewDescription.Buffer.StructureByteStride = structureByteStride;
    shaderResourceViewDescription.Buffer.Flags = bufferFlags;
    device->CreateShaderResourceView(mAllocationHandle.GetResource(), &shaderResourceViewDescription, descriptorHandle);
}

bool GraphicsVector::IsValid() const {
    return mAllocationHandle.IsValid() == true;
}

ID3D12Resource* GraphicsVector::GetResource() const {
    return mAllocationHandle.GetResource();
}

const AllocationHandle& GraphicsVector::GetAllocationHandle() const {
    return mAllocationHandle;
}

D3D12_RESOURCE_STATES GraphicsVector::GetResourceState() const {
    return mResourceState;
}

D3D12_RESOURCE_BARRIER GraphicsVector::CreateTransitionBarrier(D3D12_RESOURCE_STATES nextState) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = mAllocationHandle.GetResource();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = mResourceState;
    barrier.Transition.StateAfter = nextState;
    mResourceState = nextState;
    return barrier;
}

GraphicsVector::SizeType GraphicsVector::GetSizeInBytes() const {
    return mSizeInBytes;
}

GraphicsVector::SizeType GraphicsVector::GetCapacityInBytes() const {
    return mCapacityInBytes;
}

GraphicsVector::SizeType GraphicsVector::AlignCapacity(SizeType sizeInBytes) {
    if (sizeInBytes == 0) {
        return 0;
    }

    SizeType alignedSizeInBytes{ ((sizeInBytes + AllocationUnitSizeInBytes - 1) / AllocationUnitSizeInBytes) * AllocationUnitSizeInBytes };
    return alignedSizeInBytes;
}

bool GraphicsVector::Reallocate(GraphicsAllocator& graphicsAllocator, SizeType requiredSizeInBytes) {
    if (requiredSizeInBytes == 0) {
        return false;
    }

    SizeType newCapacityInBytes{ AlignCapacity(requiredSizeInBytes) };
    if (newCapacityInBytes == mCapacityInBytes && mAllocationHandle.IsValid() == true) {
        return true;
    }

    D3D12_RESOURCE_DESC resourceDescription{};
    resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDescription.Alignment = 0;
    resourceDescription.Width = static_cast<UINT64>(newCapacityInBytes);
    resourceDescription.Height = 1;
    resourceDescription.DepthOrArraySize = 1;
    resourceDescription.MipLevels = 1;
    resourceDescription.Format = DXGI_FORMAT_UNKNOWN;
    resourceDescription.SampleDesc.Count = 1;
    resourceDescription.SampleDesc.Quality = 0;
    resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDescription.Flags = mResourceFlags;

    Interface::AllocatePlacedResourceParameters allocationParameters{ resourceDescription, mResourceState, nullptr, L"GraphicsVector.Buffer" };
    AllocationHandle newAllocationHandle{ graphicsAllocator.AllocatePlacedResourceHandle(allocationParameters) };
    if (newAllocationHandle.IsValid() == false) {
        return false;
    }

    mAllocationHandle = std::move(newAllocationHandle);
    mCapacityInBytes = newCapacityInBytes;
    return true;
}

void GraphicsVector::TryShrink(GraphicsAllocator& graphicsAllocator) {
    if (mCopyRequestCreationCount == 0 || (mCopyRequestCreationCount % 16) != 0) {
        return;
    }

    if (mSizeInBytes == 0) {
        mAllocationHandle.Reset();
        mCapacityInBytes = 0;
        return;
    }

    if (mSizeInBytes >= mCapacityInBytes) {
        return;
    }

    SizeType shrinkCapacityInBytes{ AlignCapacity(mSizeInBytes) };
    if (shrinkCapacityInBytes >= mCapacityInBytes) {
        return;
    }

    if (mSizeInBytes * 2 >= mCapacityInBytes) {
        return;
    }

    Reallocate(graphicsAllocator, shrinkCapacityInBytes);
}
