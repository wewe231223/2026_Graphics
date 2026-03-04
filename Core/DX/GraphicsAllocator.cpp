#include "Core/DX/GraphicsAllocator.h"
#include <algorithm>
#include <bit>
#include "Utility/ErrorHandler.h"

#ifdef max
#undef max
#endif

using namespace Core::DX;

GraphicsAllocator::GraphicsAllocator()
    : mDevice{},
    mBufferState{},
    mTextureState{} {
}

GraphicsAllocator::GraphicsAllocator(ID3D12Device* device, SizeType heapSize, const D3D12_HEAP_PROPERTIES& heapProperties, D3D12_HEAP_FLAGS heapFlags)
    : GraphicsAllocator{} {
    Initialize(device, heapSize, heapProperties, heapFlags);
}

GraphicsAllocator::~GraphicsAllocator() {
    Reset();
}

GraphicsAllocator::GraphicsAllocator(GraphicsAllocator&& other) noexcept
    : mDevice{ other.mDevice },
    mBufferState{ std::move(other.mBufferState) },
    mTextureState{ std::move(other.mTextureState) } {
    other.mDevice = nullptr;
    other.ResetState(other.mBufferState);
    other.ResetState(other.mTextureState);
}

GraphicsAllocator& GraphicsAllocator::operator=(GraphicsAllocator&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    Reset();
    mDevice = other.mDevice;
    mBufferState = std::move(other.mBufferState);
    mTextureState = std::move(other.mTextureState);

    other.mDevice = nullptr;
    other.ResetState(other.mBufferState);
    other.ResetState(other.mTextureState);
    return *this;
}

bool GraphicsAllocator::Initialize(ID3D12Device* device, SizeType heapSize, const D3D12_HEAP_PROPERTIES& heapProperties, D3D12_HEAP_FLAGS heapFlags) {
    return Initialize(device, heapSize, heapSize, heapProperties, heapProperties, heapFlags, heapFlags);
}

bool GraphicsAllocator::Initialize(ID3D12Device* device, SizeType bufferHeapSize, SizeType textureHeapSize, const D3D12_HEAP_PROPERTIES& bufferHeapProperties, const D3D12_HEAP_PROPERTIES& textureHeapProperties, D3D12_HEAP_FLAGS bufferHeapFlags, D3D12_HEAP_FLAGS textureHeapFlags) {
    Reset();
    if (device == nullptr || bufferHeapSize == 0 || textureHeapSize == 0) {
        return false;
    }

    mDevice = device;
    bool bufferResult{ InitializeState(mBufferState, bufferHeapSize, bufferHeapProperties, bufferHeapFlags) };
    bool textureResult{ InitializeState(mTextureState, textureHeapSize, textureHeapProperties, textureHeapFlags) };
    if (bufferResult == false || textureResult == false) {
        Reset();
        return false;
    }

    return true;
}

void GraphicsAllocator::Reset() {
    ResetState(mBufferState);
    ResetState(mTextureState);
    mDevice = nullptr;
}

bool GraphicsAllocator::CanAllocate(const D3D12_RESOURCE_DESC& resourceDesc) const {
    Interface::GraphicsAllocationType allocationType{ ResolveAllocationType(resourceDesc) };
    return CanAllocateByType(allocationType, resourceDesc);
}

std::unique_ptr<Interface::IAllocationHandle> GraphicsAllocator::AllocatePlacedResource(const D3D12_RESOURCE_DESC& resourceDesc, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* optimizedClearValue) {
    Interface::GraphicsAllocationType allocationType{ ResolveAllocationType(resourceDesc) };
    return AllocatePlacedResource(allocationType, resourceDesc, initialState, optimizedClearValue);
}

std::unique_ptr<Interface::IAllocationHandle> GraphicsAllocator::AllocatePlacedResource(Interface::GraphicsAllocationType allocationType, const D3D12_RESOURCE_DESC& resourceDesc, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* optimizedClearValue) {
    AllocationHandle allocationHandleValue{ AllocatePlacedResourceHandle(allocationType, resourceDesc, initialState, optimizedClearValue) };
    if (allocationHandleValue.IsValid() == false) {
        return nullptr;
    }

    std::unique_ptr<AllocationHandle> handlePointer{ std::make_unique<AllocationHandle>(std::move(allocationHandleValue)) };
    return handlePointer;
}

AllocationHandle GraphicsAllocator::AllocatePlacedResourceHandle(const D3D12_RESOURCE_DESC& resourceDesc, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* optimizedClearValue) {
    Interface::GraphicsAllocationType allocationType{ ResolveAllocationType(resourceDesc) };
    return AllocatePlacedResourceHandle(allocationType, resourceDesc, initialState, optimizedClearValue);
}

AllocationHandle GraphicsAllocator::AllocatePlacedResourceHandle(Interface::GraphicsAllocationType allocationType, const D3D12_RESOURCE_DESC& resourceDesc, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* optimizedClearValue) {
    AllocationHandle emptyHandle{};
    if (mDevice == nullptr) {
        return emptyHandle;
    }
    if (CanAllocateByType(allocationType, resourceDesc) == false) {
        return emptyHandle;
    }

    HeapAllocationState& state{ GetState(allocationType) };
    if (state.Heap == nullptr) {
        return emptyHandle;
    }

    D3D12_RESOURCE_ALLOCATION_INFO allocationInfo{ mDevice->GetResourceAllocationInfo(0, 1, &resourceDesc) };
    SizeType allocationAlignment{ std::max(allocationInfo.Alignment, static_cast<uint64_t>(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)) };
    SizeType requestedSize{ AlignUp(allocationInfo.SizeInBytes, allocationAlignment) };
    int32_t blockIndex{ FindSuitableBlock(state, requestedSize, allocationAlignment) };
    if (blockIndex < 0) {
        return emptyHandle;
    }

    RemoveFromBin(state, blockIndex);
    int32_t allocatedBlockIndex{ SplitBlockWithAlignment(state, blockIndex, requestedSize, allocationAlignment) };
    if (allocatedBlockIndex < 0) {
        InsertToBin(state, blockIndex);
        return emptyHandle;
    }

    FreeBlock& allocatedBlock{ state.Blocks[allocatedBlockIndex] };
    allocatedBlock.IsFree = false;
    state.UsedSize += allocatedBlock.Size;

    ComPtr<ID3D12Resource> resource{};
    HRESULT result{ mDevice->CreatePlacedResource(state.Heap.Get(), allocatedBlock.Offset, &resourceDesc, initialState, optimizedClearValue, IID_PPV_ARGS(&resource)) };
    if (FAILED(result)) {
        allocatedBlock.IsFree = true;
        state.UsedSize -= allocatedBlock.Size;
        int32_t mergedIndex{ MergeAdjacent(state, allocatedBlockIndex) };
        InsertToBin(state, mergedIndex);
        return emptyHandle;
    }

    AllocationHandle allocationHandleValue{ this, allocationType, std::move(resource), allocatedBlock.Offset, allocatedBlock.Size };
    return allocationHandleValue;
}

ID3D12Heap* GraphicsAllocator::GetHeap() const {
    return mBufferState.Heap.Get();
}

ID3D12Heap* GraphicsAllocator::GetHeap(Interface::GraphicsAllocationType allocationType) const {
    return GetState(allocationType).Heap.Get();
}

GraphicsAllocator::SizeType GraphicsAllocator::GetHeapSize() const {
    return mBufferState.HeapSize;
}

GraphicsAllocator::SizeType GraphicsAllocator::GetHeapSize(Interface::GraphicsAllocationType allocationType) const {
    return GetState(allocationType).HeapSize;
}

GraphicsAllocator::SizeType GraphicsAllocator::GetUsedSize() const {
    return mBufferState.UsedSize;
}

GraphicsAllocator::SizeType GraphicsAllocator::GetUsedSize(Interface::GraphicsAllocationType allocationType) const {
    return GetState(allocationType).UsedSize;
}

GraphicsAllocator::OffsetType GraphicsAllocator::AlignUp(OffsetType value, OffsetType alignment) {
    if (alignment <= 1) {
        return value;
    }

    return ((value + alignment - 1) / alignment) * alignment;
}

Interface::GraphicsAllocationType GraphicsAllocator::ResolveAllocationType(const D3D12_RESOURCE_DESC& resourceDesc) {
    if (resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
        return Interface::GraphicsAllocationType::Buffer;
    }

    return Interface::GraphicsAllocationType::Texture;
}

bool GraphicsAllocator::CanAllocateByType(Interface::GraphicsAllocationType allocationType, const D3D12_RESOURCE_DESC& resourceDesc) {
    if (allocationType == Interface::GraphicsAllocationType::Buffer) {
        return resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
    }

    return resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D || resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D || resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
}

void GraphicsAllocator::ResetState(HeapAllocationState& state) {
    state.Heap.Reset();
    state.HeapSize = 0;
    state.UsedSize = 0;
    state.HeapProperties = {};
    state.HeapFlags = D3D12_HEAP_FLAG_NONE;
    state.FlBitmap.fill(0);

    for (uint32_t firstLevel{ 0 }; firstLevel < FlCount; firstLevel++) {
        state.FreeBins[firstLevel].fill(-1);
    }

    state.Blocks.clear();
    state.HeadByOffset = -1;
}

bool GraphicsAllocator::InitializeState(HeapAllocationState& state, SizeType heapSize, const D3D12_HEAP_PROPERTIES& heapProperties, D3D12_HEAP_FLAGS heapFlags) {
    ResetState(state);

    state.HeapSize = heapSize;
    state.HeapProperties = heapProperties;
    state.HeapFlags = heapFlags;

    D3D12_HEAP_DESC heapDesc{};
    heapDesc.SizeInBytes = heapSize;
    heapDesc.Properties = heapProperties;
    heapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heapDesc.Flags = heapFlags;
    ErrorHandler::report(mDevice->CreateHeap(&heapDesc, IID_PPV_ARGS(&state.Heap)), "GraphicsAllocator", "Heap Allocation Failed!", ErrorHandler::Level::Critical);

    int32_t rootBlockIndex{ CreateBlock(state, 0, heapSize, true) };
    state.HeadByOffset = rootBlockIndex;
    InsertToBin(state, rootBlockIndex);
    return true;
}

void GraphicsAllocator::FreeAllocation(Interface::GraphicsAllocationType allocationType, OffsetType offset, SizeType size) {
    HeapAllocationState& state{ GetState(allocationType) };
    FreeAllocation(state, offset, size);
}

void GraphicsAllocator::FreeAllocation(HeapAllocationState& state, OffsetType offset, SizeType size) {
    if (size == 0) {
        return;
    }

    int32_t blockIndex{ FindBlockByOffset(state, offset) };
    if (blockIndex < 0) {
        return;
    }

    FreeBlock& block{ state.Blocks[blockIndex] };
    if (block.IsFree || block.Size != size) {
        return;
    }

    block.IsFree = true;
    state.UsedSize -= block.Size;
    int32_t mergedIndex{ MergeAdjacent(state, blockIndex) };
    InsertToBin(state, mergedIndex);
}

uint32_t GraphicsAllocator::MappingInsert(SizeType size, uint32_t& firstLevel, uint32_t& secondLevel) const {
    uint64_t normalizedSize{ std::max(size, static_cast<uint64_t>(1)) };
    firstLevel = static_cast<uint32_t>(std::bit_width(normalizedSize) - 1);
    if (firstLevel >= FlCount) {
        firstLevel = FlCount - 1;
    }

    uint64_t base{ static_cast<uint64_t>(1) << firstLevel };
    uint64_t delta{ normalizedSize - base };
    uint64_t step{ (base >> SlShift) == 0 ? 1 : (base >> SlShift) };

    secondLevel = static_cast<uint32_t>(delta / step);
    if (secondLevel > SlMask) {
        secondLevel = SlMask;
    }

    return static_cast<uint32_t>((firstLevel << SlShift) | secondLevel);
}

void GraphicsAllocator::InsertToBin(HeapAllocationState& state, int32_t blockIndex) {
    if (blockIndex < 0) {
        return;
    }

    FreeBlock& block{ state.Blocks[blockIndex] };
    if (block.IsFree == false || block.Size == 0) {
        return;
    }

    uint32_t firstLevel{ 0 };
    uint32_t secondLevel{ 0 };
    MappingInsert(block.Size, firstLevel, secondLevel);
    int32_t currentHead{ state.FreeBins[firstLevel][secondLevel] };

    block.PrevByBin = -1;
    block.NextByBin = currentHead;
    if (currentHead >= 0) {
        state.Blocks[currentHead].PrevByBin = blockIndex;
    }

    state.FreeBins[firstLevel][secondLevel] = blockIndex;
    state.FlBitmap[firstLevel] |= (1u << secondLevel);
}

void GraphicsAllocator::RemoveFromBin(HeapAllocationState& state, int32_t blockIndex) {
    if (blockIndex < 0) {
        return;
    }

    FreeBlock& block{ state.Blocks[blockIndex] };
    if (block.IsFree == false || block.Size == 0) {
        return;
    }

    uint32_t firstLevel{ 0 };
    uint32_t secondLevel{ 0 };
    MappingInsert(block.Size, firstLevel, secondLevel);

    int32_t prevIndex{ block.PrevByBin };
    int32_t nextIndex{ block.NextByBin };

    if (prevIndex >= 0) {
        state.Blocks[prevIndex].NextByBin = nextIndex;
    }
    else {
        state.FreeBins[firstLevel][secondLevel] = nextIndex;
    }

    if (nextIndex >= 0) {
        state.Blocks[nextIndex].PrevByBin = prevIndex;
    }

    if (state.FreeBins[firstLevel][secondLevel] < 0) {
        state.FlBitmap[firstLevel] &= ~(1u << secondLevel);
    }

    block.PrevByBin = -1;
    block.NextByBin = -1;
}

int32_t GraphicsAllocator::FindSuitableBlock(const HeapAllocationState& state, SizeType size, SizeType alignment) const {
    uint32_t firstLevel{ 0 };
    uint32_t secondLevel{ 0 };
    MappingInsert(size, firstLevel, secondLevel);

    for (uint32_t first{ firstLevel }; first < FlCount; first++) {
        uint32_t startSecond{ first == firstLevel ? secondLevel : 0 };
        uint32_t bitset{ state.FlBitmap[first] & (~((1u << startSecond) - 1)) };
        if (bitset == 0) {
            continue;
        }

        uint32_t chosenSecond{ static_cast<uint32_t>(std::countr_zero(bitset)) };
        int32_t headIndex{ state.FreeBins[first][chosenSecond] };
        while (headIndex >= 0) {
            const FreeBlock& block{ state.Blocks[headIndex] };
            OffsetType alignedOffset{ AlignUp(block.Offset, alignment) };
            SizeType paddingSize{ alignedOffset - block.Offset };
            SizeType totalNeed{ paddingSize + size };
            if (block.IsFree && block.Size >= totalNeed) {
                return headIndex;
            }
            headIndex = block.NextByBin;
        }
    }

    return -1;
}

int32_t GraphicsAllocator::CreateBlock(HeapAllocationState& state, OffsetType offset, SizeType size, bool isFree) {
    FreeBlock block{};
    block.Offset = offset;
    block.Size = size;
    block.PrevByOffset = -1;
    block.NextByOffset = -1;
    block.PrevByBin = -1;
    block.NextByBin = -1;
    block.IsFree = isFree;

    state.Blocks.push_back(block);
    return static_cast<int32_t>(state.Blocks.size() - 1);
}

int32_t GraphicsAllocator::FindBlockByOffset(const HeapAllocationState& state, OffsetType offset) const {
    int32_t index{ state.HeadByOffset };
    while (index >= 0) {
        const FreeBlock& block{ state.Blocks[index] };
        if (block.Offset == offset) {
            return index;
        }
        if (block.Offset > offset) {
            return -1;
        }

        index = block.NextByOffset;
    }

    return -1;
}

int32_t GraphicsAllocator::InsertBlockAfter(HeapAllocationState& state, int32_t prevBlockIndex, OffsetType offset, SizeType size, bool isFree) {
    int32_t newBlockIndex{ CreateBlock(state, offset, size, isFree) };
    if (prevBlockIndex < 0) {
        int32_t oldHead{ state.HeadByOffset };
        state.Blocks[newBlockIndex].PrevByOffset = -1;
        state.Blocks[newBlockIndex].NextByOffset = oldHead;
        if (oldHead >= 0) {
            state.Blocks[oldHead].PrevByOffset = newBlockIndex;
        }
        state.HeadByOffset = newBlockIndex;
        return newBlockIndex;
    }

    int32_t nextIndex{ state.Blocks[prevBlockIndex].NextByOffset };
    state.Blocks[newBlockIndex].PrevByOffset = prevBlockIndex;
    state.Blocks[newBlockIndex].NextByOffset = nextIndex;
    state.Blocks[prevBlockIndex].NextByOffset = newBlockIndex;
    if (nextIndex >= 0) {
        state.Blocks[nextIndex].PrevByOffset = newBlockIndex;
    }

    return newBlockIndex;
}

int32_t GraphicsAllocator::SplitBlockWithAlignment(HeapAllocationState& state, int32_t blockIndex, SizeType requestedSize, SizeType alignment) {
    if (blockIndex < 0) {
        return -1;
    }

    FreeBlock originalBlock{ state.Blocks[blockIndex] };
    OffsetType alignedOffset{ AlignUp(originalBlock.Offset, alignment) };
    SizeType prefixSize{ alignedOffset - originalBlock.Offset };
    SizeType totalRequired{ prefixSize + requestedSize };
    if (originalBlock.Size < totalRequired) {
        return -1;
    }

    SizeType suffixSize{ originalBlock.Size - totalRequired };
    state.Blocks[blockIndex].Offset = alignedOffset;
    state.Blocks[blockIndex].Size = requestedSize;
    state.Blocks[blockIndex].IsFree = false;
    state.Blocks[blockIndex].PrevByBin = -1;
    state.Blocks[blockIndex].NextByBin = -1;

    if (prefixSize > 0) {
        int32_t prefixIndex{ InsertBlockAfter(state, originalBlock.PrevByOffset, originalBlock.Offset, prefixSize, true) };
        if (state.Blocks[prefixIndex].NextByOffset != blockIndex) {
            state.Blocks[prefixIndex].NextByOffset = blockIndex;
            state.Blocks[blockIndex].PrevByOffset = prefixIndex;
        }
        InsertToBin(state, prefixIndex);
    }
    else {
        state.Blocks[blockIndex].PrevByOffset = originalBlock.PrevByOffset;
    }

    if (suffixSize > 0) {
        int32_t suffixIndex{ InsertBlockAfter(state, blockIndex, alignedOffset + requestedSize, suffixSize, true) };
        InsertToBin(state, suffixIndex);
    }
    else {
        state.Blocks[blockIndex].NextByOffset = originalBlock.NextByOffset;
        if (originalBlock.NextByOffset >= 0) {
            state.Blocks[originalBlock.NextByOffset].PrevByOffset = blockIndex;
        }
    }

    if (state.Blocks[blockIndex].PrevByOffset < 0) {
        state.HeadByOffset = blockIndex;
    }

    return blockIndex;
}

int32_t GraphicsAllocator::MergeAdjacent(HeapAllocationState& state, int32_t blockIndex) {
    int32_t currentIndex{ blockIndex };
    int32_t prevIndex{ state.Blocks[currentIndex].PrevByOffset };

    if (prevIndex >= 0 && state.Blocks[prevIndex].IsFree) {
        RemoveFromBin(state, prevIndex);
        SizeType currentSize{ state.Blocks[currentIndex].Size };
        int32_t nextIndex{ state.Blocks[currentIndex].NextByOffset };
        state.Blocks[prevIndex].Size += currentSize;
        state.Blocks[prevIndex].NextByOffset = nextIndex;
        if (nextIndex >= 0) {
            state.Blocks[nextIndex].PrevByOffset = prevIndex;
        }

        state.Blocks[currentIndex].Size = 0;
        state.Blocks[currentIndex].IsFree = false;
        state.Blocks[currentIndex].PrevByOffset = -1;
        state.Blocks[currentIndex].NextByOffset = -1;
        currentIndex = prevIndex;
    }

    int32_t nextIndex{ state.Blocks[currentIndex].NextByOffset };
    if (nextIndex >= 0 && state.Blocks[nextIndex].IsFree) {
        RemoveFromBin(state, nextIndex);
        state.Blocks[currentIndex].Size += state.Blocks[nextIndex].Size;
        state.Blocks[currentIndex].NextByOffset = state.Blocks[nextIndex].NextByOffset;
        if (state.Blocks[nextIndex].NextByOffset >= 0) {
            state.Blocks[state.Blocks[nextIndex].NextByOffset].PrevByOffset = currentIndex;
        }

        state.Blocks[nextIndex].Size = 0;
        state.Blocks[nextIndex].IsFree = false;
        state.Blocks[nextIndex].PrevByOffset = -1;
        state.Blocks[nextIndex].NextByOffset = -1;
    }

    if (state.Blocks[currentIndex].PrevByOffset < 0) {
        state.HeadByOffset = currentIndex;
    }

    return currentIndex;
}

GraphicsAllocator::HeapAllocationState& GraphicsAllocator::GetState(Interface::GraphicsAllocationType allocationType) {
    if (allocationType == Interface::GraphicsAllocationType::Texture) {
        return mTextureState;
    }

    return mBufferState;
}

const GraphicsAllocator::HeapAllocationState& GraphicsAllocator::GetState(Interface::GraphicsAllocationType allocationType) const {
    if (allocationType == Interface::GraphicsAllocationType::Texture) {
        return mTextureState;
    }

    return mBufferState;
}
