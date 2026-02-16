#include "GraphicsBuffer.h"
#include <algorithm>
#include <utility>

using namespace Core::DX;

GraphicsBuffer::GraphicsBuffer()
    : mAllocator{},
    mAllocationHandle{},
    mUploadData{},
    mSizeInBytes{},
    mResourceState{ D3D12_RESOURCE_STATE_COMMON } {
}

GraphicsBuffer::GraphicsBuffer(ID3D12Device*)
    : GraphicsBuffer{} {
}

GraphicsBuffer::GraphicsBuffer(ID3D12Device* device, SizeType sizeInBytes, D3D12_RESOURCE_FLAGS resourceFlags, D3D12_RESOURCE_STATES initialState)
    : GraphicsBuffer{} {
    Initialize(device, sizeInBytes, resourceFlags, initialState);
}

GraphicsBuffer::~GraphicsBuffer() {
    Reset();
}

GraphicsBuffer::GraphicsBuffer(GraphicsBuffer&& other) noexcept
    : mAllocator{ std::move(other.mAllocator) },
    mAllocationHandle{ std::move(other.mAllocationHandle) },
    mUploadData{ std::move(other.mUploadData) },
    mSizeInBytes{ other.mSizeInBytes },
    mResourceState{ other.mResourceState } {
    other.mSizeInBytes = 0;
    other.mResourceState = D3D12_RESOURCE_STATE_COMMON;
}

GraphicsBuffer& GraphicsBuffer::operator=(GraphicsBuffer&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    Reset();
    mAllocator = std::move(other.mAllocator);
    mAllocationHandle = std::move(other.mAllocationHandle);
    mUploadData = std::move(other.mUploadData);
    mSizeInBytes = other.mSizeInBytes;
    mResourceState = other.mResourceState;
    other.mSizeInBytes = 0;
    other.mResourceState = D3D12_RESOURCE_STATE_COMMON;
    return *this;
}

bool GraphicsBuffer::Initialize(ID3D12Device* device, SizeType sizeInBytes, D3D12_RESOURCE_FLAGS resourceFlags, D3D12_RESOURCE_STATES initialState) {
    Reset();

    if (device == nullptr || sizeInBytes == 0) {
        return false;
    }

    D3D12_HEAP_PROPERTIES HeapProperties{};
    HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    HeapProperties.CreationNodeMask = 1;
    HeapProperties.VisibleNodeMask = 1;

    bool InitializeResult{ mAllocator.Initialize(device, static_cast<uint64_t>(sizeInBytes), HeapProperties, D3D12_HEAP_FLAG_NONE) };
    if (InitializeResult == false) {
        return false;
    }

    D3D12_RESOURCE_DESC ResourceDescription{};
    ResourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    ResourceDescription.Alignment = 0;
    ResourceDescription.Width = static_cast<UINT64>(sizeInBytes);
    ResourceDescription.Height = 1;
    ResourceDescription.DepthOrArraySize = 1;
    ResourceDescription.MipLevels = 1;
    ResourceDescription.Format = DXGI_FORMAT_UNKNOWN;
    ResourceDescription.SampleDesc.Count = 1;
    ResourceDescription.SampleDesc.Quality = 0;
    ResourceDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ResourceDescription.Flags = resourceFlags;

    mAllocationHandle = mAllocator.AllocatePlacedResource(ResourceDescription, initialState, nullptr);
    if (mAllocationHandle.IsValid() == false) {
        Reset();
        return false;
    }

    mUploadData = std::make_unique<ValueType[]>(sizeInBytes);
    mSizeInBytes = sizeInBytes;
    mResourceState = initialState;
    return true;
}

void GraphicsBuffer::Reset() {
    mUploadData.reset();
    mAllocationHandle.Reset();
    mAllocator.Reset();
    mSizeInBytes = 0;
    mResourceState = D3D12_RESOURCE_STATE_COMMON;
}

GraphicsBuffer::Iterator GraphicsBuffer::begin() {
    return mUploadData.get();
}

GraphicsBuffer::ConstIterator GraphicsBuffer::begin() const {
    return mUploadData.get();
}

GraphicsBuffer::ConstIterator GraphicsBuffer::cbegin() const {
    return mUploadData.get();
}

GraphicsBuffer::Iterator GraphicsBuffer::end() {
    return mUploadData.get() + mSizeInBytes;
}

GraphicsBuffer::ConstIterator GraphicsBuffer::end() const {
    return mUploadData.get() + mSizeInBytes;
}

GraphicsBuffer::ConstIterator GraphicsBuffer::cend() const {
    return mUploadData.get() + mSizeInBytes;
}

GraphicsBuffer::ReverseIterator GraphicsBuffer::rbegin() {
    return ReverseIterator{ end() };
}

GraphicsBuffer::ConstReverseIterator GraphicsBuffer::rbegin() const {
    return ConstReverseIterator{ end() };
}

GraphicsBuffer::ConstReverseIterator GraphicsBuffer::crbegin() const {
    return ConstReverseIterator{ cend() };
}

GraphicsBuffer::ReverseIterator GraphicsBuffer::rend() {
    return ReverseIterator{ begin() };
}

GraphicsBuffer::ConstReverseIterator GraphicsBuffer::rend() const {
    return ConstReverseIterator{ begin() };
}

GraphicsBuffer::ConstReverseIterator GraphicsBuffer::crend() const {
    return ConstReverseIterator{ cbegin() };
}

GraphicsBuffer::Reference GraphicsBuffer::operator[](SizeType index) {
    return mUploadData[index];
}

GraphicsBuffer::ConstReference GraphicsBuffer::operator[](SizeType index) const {
    return mUploadData[index];
}

GraphicsBuffer::Reference GraphicsBuffer::front() {
    return mUploadData[0];
}

GraphicsBuffer::ConstReference GraphicsBuffer::front() const {
    return mUploadData[0];
}

GraphicsBuffer::Reference GraphicsBuffer::back() {
    return mUploadData[mSizeInBytes - 1];
}

GraphicsBuffer::ConstReference GraphicsBuffer::back() const {
    return mUploadData[mSizeInBytes - 1];
}

GraphicsBuffer::Pointer GraphicsBuffer::data() {
    return mUploadData.get();
}

GraphicsBuffer::ConstPointer GraphicsBuffer::data() const {
    return mUploadData.get();
}

bool GraphicsBuffer::empty() const {
    return mSizeInBytes == 0;
}

GraphicsBuffer::SizeType GraphicsBuffer::size() const {
    return mSizeInBytes;
}

GraphicsBuffer::SizeType GraphicsBuffer::max_size() const {
    return mSizeInBytes;
}

void GraphicsBuffer::fill(ValueType value) {
    std::fill(begin(), end(), value);
}

void GraphicsBuffer::swap(GraphicsBuffer& other) noexcept {
    std::swap(mAllocator, other.mAllocator);
    std::swap(mAllocationHandle, other.mAllocationHandle);
    std::swap(mUploadData, other.mUploadData);
    std::swap(mSizeInBytes, other.mSizeInBytes);
    std::swap(mResourceState, other.mResourceState);
}

bool GraphicsBuffer::IsValid() const {
    return mAllocationHandle.IsValid() == true && mUploadData != nullptr && mSizeInBytes > 0;
}

ID3D12Resource* GraphicsBuffer::GetResource() const {
    return mAllocationHandle.GetResource().Get();
}

const AllocationHandle& GraphicsBuffer::GetAllocationHandle() const {
    return mAllocationHandle;
}

D3D12_RESOURCE_STATES GraphicsBuffer::GetResourceState() const {
    return mResourceState;
}

D3D12_RESOURCE_BARRIER GraphicsBuffer::CreateTransitionBarrier(D3D12_RESOURCE_STATES nextState) {
    D3D12_RESOURCE_BARRIER Barrier{};
    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    Barrier.Transition.pResource = mAllocationHandle.GetResource().Get();
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = mResourceState;
    Barrier.Transition.StateAfter = nextState;
    mResourceState = nextState;
    return Barrier;
}

CopyQueueCopyRequest GraphicsBuffer::CreateCopyRequest(UINT64 destinationOffset) const {
    CopyQueueCopyRequest CopyRequest{};
    CopyRequest.DestinationDefaultResource = mAllocationHandle.GetResource();
    CopyRequest.DestinationOffset = destinationOffset;
    CopyRequest.SourceData.resize(mSizeInBytes);
    std::copy(cbegin(), cend(), CopyRequest.SourceData.begin());
    return CopyRequest;
}
