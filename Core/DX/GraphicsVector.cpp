#include "GraphicsVector.h"
#include <algorithm>

using namespace Core::DX;

GraphicsVector::GraphicsVector()
    : mAllocationHandle{},
    mSizeInBytes{},
    mCapacityInBytes{},
    mResourceFlags{ D3D12_RESOURCE_FLAG_NONE },
    mCopyRequestCreationCount{} {
}


GraphicsVector::GraphicsVector(GraphicsAllocator& GraphicsAllocator, SizeType InitialSizeInBytes, D3D12_RESOURCE_FLAGS ResourceFlags)
    : GraphicsVector{} {
    Initialize(GraphicsAllocator, InitialSizeInBytes, ResourceFlags);
}
GraphicsVector::~GraphicsVector() {
    Reset();
}

GraphicsVector::GraphicsVector(GraphicsVector&& other) noexcept
    : mAllocationHandle{ std::move(other.mAllocationHandle) },
    mSizeInBytes{ other.mSizeInBytes },
    mCapacityInBytes{ other.mCapacityInBytes },
    mResourceFlags{ other.mResourceFlags },
    mCopyRequestCreationCount{ other.mCopyRequestCreationCount } {
    other.mSizeInBytes = 0;
    other.mCapacityInBytes = 0;
    other.mResourceFlags = D3D12_RESOURCE_FLAG_NONE;
    other.mCopyRequestCreationCount = 0;
}

GraphicsVector& GraphicsVector::operator=(GraphicsVector&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    Reset();
    mAllocationHandle = std::move(other.mAllocationHandle);
    mSizeInBytes = other.mSizeInBytes;
    mCapacityInBytes = other.mCapacityInBytes;
    mResourceFlags = other.mResourceFlags;
    mCopyRequestCreationCount = other.mCopyRequestCreationCount;

    other.mSizeInBytes = 0;
    other.mCapacityInBytes = 0;
    other.mResourceFlags = D3D12_RESOURCE_FLAG_NONE;
    other.mCopyRequestCreationCount = 0;
    return *this;
}

bool GraphicsVector::Initialize(GraphicsAllocator& GraphicsAllocator, SizeType InitialSizeInBytes, D3D12_RESOURCE_FLAGS ResourceFlags) {
    Reset();

    mResourceFlags = ResourceFlags;
    mSizeInBytes = InitialSizeInBytes;
    mCopyRequestCreationCount = 0;

    if (InitialSizeInBytes == 0) {
        mCapacityInBytes = 0;
        return true;
    }

    return Reallocate(GraphicsAllocator, InitialSizeInBytes);
}
bool GraphicsVector::Resize(GraphicsAllocator& graphicsAllocator, SizeType sizeInBytes) {
    if (sizeInBytes > mCapacityInBytes) {
        bool reallocateResult{ Reallocate(graphicsAllocator, sizeInBytes) };
        if (reallocateResult == false) {
            return false;
        }
    }

    mSizeInBytes = sizeInBytes;
    return true;
}

void GraphicsVector::Reset() {
    mAllocationHandle.Reset();
    mSizeInBytes = 0;
    mCapacityInBytes = 0;
    mResourceFlags = D3D12_RESOURCE_FLAG_NONE;
    mCopyRequestCreationCount = 0;
}

bool GraphicsVector::PrepareCopyRequest(GraphicsAllocator& GraphicsAllocator, std::span<const std::byte> SourceData, Interface::CopyPriority Priority, Interface::CopyRequest& OutCopyRequest, UINT64 DestinationOffset) {
    mCopyRequestCreationCount += 1;

    bool ResizeResult{ Resize(GraphicsAllocator, SourceData.size()) };
    if (ResizeResult == false) {
        return false;
    }

    TryShrink(GraphicsAllocator);

    OutCopyRequest = Interface::CopyRequest{ Priority };
    OutCopyRequest.DestinationDefaultResource = mAllocationHandle.GetResourceComPtr();
    OutCopyRequest.DestinationOffset = DestinationOffset;
    OutCopyRequest.SourceData = SourceData;
    return true;
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

    Interface::AllocatePlacedResourceParameters allocationParameters{ resourceDescription, D3D12_RESOURCE_STATE_COMMON, nullptr, L"GraphicsVector.Buffer" };
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
