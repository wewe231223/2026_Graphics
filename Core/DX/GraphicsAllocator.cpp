#include "Core/DX/GraphicsAllocator.h"
#include <algorithm>
#include <bit>
#include <string>
#include "Utility/ErrorHandler.h"

#ifdef max 
#undef max
#endif 

using namespace Core::DX;

GraphicsAllocator::GraphicsAllocator()
    : mDevice{},
    mHeap{},
    mHeapSize{},
    mUsedSize{},
    mHeapProperties{},
    mHeapFlags{ D3D12_HEAP_FLAG_NONE },
    mFlBitmap{},
    mFreeBins{},
    mBlocks{},
    mHeadByOffset{ -1 } {
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
    mHeap{ std::move(other.mHeap) },
    mHeapSize{ other.mHeapSize },
    mUsedSize{ other.mUsedSize },
    mHeapProperties{ other.mHeapProperties },
    mHeapFlags{ other.mHeapFlags },
    mFlBitmap{ other.mFlBitmap },
    mFreeBins{ other.mFreeBins },
    mBlocks{ std::move(other.mBlocks) },
    mHeadByOffset{ other.mHeadByOffset } {

    other.mDevice = nullptr;
    other.mHeapSize = 0;
    other.mUsedSize = 0;
    other.mHeapFlags = D3D12_HEAP_FLAG_NONE;
    other.mFlBitmap.fill(0);

    for (uint32_t firstLevel = 0; firstLevel < FlCount; ++firstLevel) {
        other.mFreeBins[firstLevel].fill(-1);
    }

    other.mHeadByOffset = -1;
}

GraphicsAllocator& GraphicsAllocator::operator=(GraphicsAllocator&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    Reset();
    mDevice = other.mDevice;
    mHeap = std::move(other.mHeap);
    mHeapSize = other.mHeapSize;
    mUsedSize = other.mUsedSize;
    mHeapProperties = other.mHeapProperties;
    mHeapFlags = other.mHeapFlags;
    mFlBitmap = other.mFlBitmap;
    mFreeBins = other.mFreeBins;
    mBlocks = std::move(other.mBlocks);
    mHeadByOffset = other.mHeadByOffset;

    other.mDevice = nullptr;
    other.mHeapSize = 0;
    other.mUsedSize = 0;
    other.mHeapFlags = D3D12_HEAP_FLAG_NONE;
    other.mFlBitmap.fill(0);

    for (uint32_t firstLevel = 0; firstLevel < FlCount; ++firstLevel) {
        other.mFreeBins[firstLevel].fill(-1);
    }

    other.mHeadByOffset = -1;
    return *this;
}

bool GraphicsAllocator::Initialize(ID3D12Device* device, SizeType heapSize, const D3D12_HEAP_PROPERTIES& heapProperties, D3D12_HEAP_FLAGS heapFlags) {
    Reset();
    if (device == nullptr || heapSize == 0) {
        return false;
    }
    mDevice = device;
    mHeapSize = heapSize;
    mHeapProperties = heapProperties;
    mHeapFlags = heapFlags;

    for (uint32_t firstLevel = 0; firstLevel < FlCount; ++firstLevel) {
        mFreeBins[firstLevel].fill(-1);
    }

    D3D12_HEAP_DESC heapDesc{};
    heapDesc.SizeInBytes = heapSize;
    heapDesc.Properties = heapProperties;
    heapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heapDesc.Flags = heapFlags;

    ErrorHandler::report(mDevice->CreateHeap(&heapDesc, IID_PPV_ARGS(&mHeap)),"GraphicsAllocator", "Heap Allocation Failed!", ErrorHandler::Level::Critical);

    int32_t rootBlockIndex{ CreateBlock(0, heapSize, true) };

    mHeadByOffset = rootBlockIndex;
    InsertToBin(rootBlockIndex);
    return true;
}

void GraphicsAllocator::Reset() {
    mHeap.Reset();
    mBlocks.clear();
    mFlBitmap.fill(0);

    for (uint32_t firstLevel = 0; firstLevel < FlCount; ++firstLevel) {
        mFreeBins[firstLevel].fill(-1);
    }

    mDevice = nullptr;
    mHeapSize = 0;
    mUsedSize = 0;
    mHeapProperties = {};
    mHeapFlags = D3D12_HEAP_FLAG_NONE;
    mHeadByOffset = -1;
}

bool GraphicsAllocator::CanAllocate(const D3D12_RESOURCE_DESC& resourceDesc) const {
    bool isBuffer{ resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER };
    bool isTexture{ resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D || resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D || resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D };

    if (isBuffer) {
        return true;
    }
    if (isTexture) {
        return true;
    }

    return false;
}

std::unique_ptr<Interface::IAllocationHandle> GraphicsAllocator::AllocatePlacedResource(const D3D12_RESOURCE_DESC& resourceDesc, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* optimizedClearValue) {
    AllocationHandle AllocationHandleValue{ AllocatePlacedResourceHandle(resourceDesc, initialState, optimizedClearValue) };
    if (AllocationHandleValue.IsValid() == false) {
        return nullptr;
    }

    std::unique_ptr<AllocationHandle> HandlePointer{ std::make_unique<AllocationHandle>(std::move(AllocationHandleValue)) };
    return HandlePointer;
}

AllocationHandle GraphicsAllocator::AllocatePlacedResourceHandle(const D3D12_RESOURCE_DESC& resourceDesc, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* optimizedClearValue) {
    AllocationHandle EmptyHandle{};

    if (mDevice == nullptr || mHeap == nullptr) {
        return EmptyHandle;
    }
    if (CanAllocate(resourceDesc) == false) {
        return EmptyHandle;
    }

    D3D12_RESOURCE_ALLOCATION_INFO allocationInfo{ mDevice->GetResourceAllocationInfo(0, 1, &resourceDesc) };
    SizeType allocationAlignment{ std::max(allocationInfo.Alignment, static_cast<uint64_t>(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)) };
    SizeType requestedSize{ AlignUp(allocationInfo.SizeInBytes, allocationAlignment) };
    int32_t blockIndex{ FindSuitableBlock(requestedSize, allocationAlignment) };
    if (blockIndex < 0) {
        return EmptyHandle;
    }
    RemoveFromBin(blockIndex);
    int32_t allocatedBlockIndex{ SplitBlockWithAlignment(blockIndex, requestedSize, allocationAlignment) };
    if (allocatedBlockIndex < 0) {
        InsertToBin(blockIndex);
        return EmptyHandle;
    }

    FreeBlock& allocatedBlock = mBlocks[allocatedBlockIndex];
    allocatedBlock.IsFree = false;
    mUsedSize += allocatedBlock.Size;

    ComPtr<ID3D12Resource> resource{};
    HRESULT result{ mDevice->CreatePlacedResource(mHeap.Get(), allocatedBlock.Offset, &resourceDesc, initialState, optimizedClearValue, IID_PPV_ARGS(&resource)) };

    if (FAILED(result)) {
        allocatedBlock.IsFree = true;
        mUsedSize -= allocatedBlock.Size;
        int32_t mergedIndex{ MergeAdjacent(allocatedBlockIndex) };
        InsertToBin(mergedIndex);
        return EmptyHandle;
    }

    std::wstring resourceName{ L"GraphicsAllocator.PlacedResource.Offset_" + std::to_wstring(allocatedBlock.Offset) + L".Size_" + std::to_wstring(allocatedBlock.Size) };
    resource->SetName(resourceName.c_str());

    AllocationHandle AllocationHandleValue{ this, std::move(resource), allocatedBlock.Offset, allocatedBlock.Size };
    return AllocationHandleValue;
}

ID3D12Heap* GraphicsAllocator::GetHeap() const {
    return mHeap.Get();
}

GraphicsAllocator::SizeType GraphicsAllocator::GetHeapSize() const {
    return mHeapSize;
}

GraphicsAllocator::SizeType GraphicsAllocator::GetUsedSize() const {
    return mUsedSize;
}

GraphicsAllocator::OffsetType GraphicsAllocator::AlignUp(OffsetType value, OffsetType alignment) {
    if (alignment <= 1) {
        return value;
    }
    return ((value + alignment - 1) / alignment) * alignment;
}

void GraphicsAllocator::FreeAllocation(OffsetType offset, SizeType size) {
    if (size == 0) {
        return;
    }

    int32_t blockIndex{ FindBlockByOffset(offset) };
    if (blockIndex < 0) {
        return;
    }

    FreeBlock& block = mBlocks[blockIndex];
    if (block.IsFree || block.Size != size) {
        return;
    }

    block.IsFree = true;
    mUsedSize -= block.Size;
    int32_t mergedIndex{ MergeAdjacent(blockIndex) };
    InsertToBin(mergedIndex);
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

void GraphicsAllocator::InsertToBin(int32_t blockIndex) {
    if (blockIndex < 0) {
        return;
    }

    FreeBlock& block = mBlocks[blockIndex];
    if (block.IsFree == false || block.Size == 0) {
        return;
    }

    uint32_t firstLevel{ 0 };
    uint32_t secondLevel{ 0 };

    MappingInsert(block.Size, firstLevel, secondLevel);
    int32_t currentHead{ mFreeBins[firstLevel][secondLevel] };

    block.PrevByBin = -1;
    block.NextByBin = currentHead;
    if (currentHead >= 0) {
        mBlocks[currentHead].PrevByBin = blockIndex;
    }
    mFreeBins[firstLevel][secondLevel] = blockIndex;
    mFlBitmap[firstLevel] |= (1u << secondLevel);
}

void GraphicsAllocator::RemoveFromBin(int32_t blockIndex) {
    if (blockIndex < 0) {
        return;
    }

    FreeBlock& block = mBlocks[blockIndex];
    if (block.IsFree == false || block.Size == 0) {
        return;
    }

    uint32_t firstLevel{ 0 };
    uint32_t secondLevel{ 0 };
    MappingInsert(block.Size, firstLevel, secondLevel);

    int32_t prevIndex{ block.PrevByBin };
    int32_t nextIndex{ block.NextByBin };

    if (prevIndex >= 0) {
        mBlocks[prevIndex].NextByBin = nextIndex;
    }
    else {
        mFreeBins[firstLevel][secondLevel] = nextIndex;
    }

    if (nextIndex >= 0) {
        mBlocks[nextIndex].PrevByBin = prevIndex;
    }

    if (mFreeBins[firstLevel][secondLevel] < 0) {
        mFlBitmap[firstLevel] &= ~(1u << secondLevel);
    }

    block.PrevByBin = -1;
    block.NextByBin = -1;
}

int32_t GraphicsAllocator::FindSuitableBlock(SizeType size, SizeType alignment) const {
    uint32_t firstLevel{ 0 };
    uint32_t secondLevel{ 0 };
    MappingInsert(size, firstLevel, secondLevel);

    for (uint32_t first = firstLevel; first < FlCount; ++first) {
        uint32_t startSecond{ first == firstLevel ? secondLevel : 0 };
        uint32_t bitset{ mFlBitmap[first] & (~((1u << startSecond) - 1)) };
        if (bitset == 0) {
            continue;
        }

        uint32_t chosenSecond{ static_cast<uint32_t>(std::countr_zero(bitset)) };
        int32_t headIndex{ mFreeBins[first][chosenSecond] };

        while (headIndex >= 0) {
            const FreeBlock& block = mBlocks[headIndex];
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

int32_t GraphicsAllocator::CreateBlock(OffsetType offset, SizeType size, bool isFree) {
    FreeBlock block{};
    block.Offset = offset;
    block.Size = size;
    block.PrevByOffset = -1;
    block.NextByOffset = -1;
    block.PrevByBin = -1;
    block.NextByBin = -1;
    block.IsFree = isFree;

    mBlocks.push_back(block);
    return static_cast<int32_t>(mBlocks.size() - 1);
}

int32_t GraphicsAllocator::FindBlockByOffset(OffsetType offset) const {
    int32_t index{ mHeadByOffset };
    while (index >= 0) {
        const FreeBlock& block = mBlocks[index];
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

int32_t GraphicsAllocator::InsertBlockAfter(int32_t prevBlockIndex, OffsetType offset, SizeType size, bool isFree) {
    int32_t newBlockIndex{ CreateBlock(offset, size, isFree) };
    if (prevBlockIndex < 0) {
        int32_t oldHead{ mHeadByOffset };
        mBlocks[newBlockIndex].PrevByOffset = -1;
        mBlocks[newBlockIndex].NextByOffset = oldHead;
        if (oldHead >= 0) {
            mBlocks[oldHead].PrevByOffset = newBlockIndex;
        }
        mHeadByOffset = newBlockIndex;
        return newBlockIndex;
    }

    int32_t nextIndex{ mBlocks[prevBlockIndex].NextByOffset };
    mBlocks[newBlockIndex].PrevByOffset = prevBlockIndex;
    mBlocks[newBlockIndex].NextByOffset = nextIndex;
    mBlocks[prevBlockIndex].NextByOffset = newBlockIndex;

    if (nextIndex >= 0) {
        mBlocks[nextIndex].PrevByOffset = newBlockIndex;
    }

    return newBlockIndex;
}

int32_t GraphicsAllocator::SplitBlockWithAlignment(int32_t blockIndex, SizeType requestedSize, SizeType alignment) {
    if (blockIndex < 0) {
        return -1;
    }

    FreeBlock originalBlock{ mBlocks[blockIndex] };
    OffsetType alignedOffset{ AlignUp(originalBlock.Offset, alignment) };
    SizeType prefixSize{ alignedOffset - originalBlock.Offset };
    SizeType totalRequired{ prefixSize + requestedSize };
    if (originalBlock.Size < totalRequired) {
        return -1;
    }

    SizeType suffixSize{ originalBlock.Size - totalRequired };
    mBlocks[blockIndex].Offset = alignedOffset;
    mBlocks[blockIndex].Size = requestedSize;
    mBlocks[blockIndex].IsFree = false;
    mBlocks[blockIndex].PrevByBin = -1;
    mBlocks[blockIndex].NextByBin = -1;

    if (prefixSize > 0) {
        int32_t prefixIndex{ InsertBlockAfter(originalBlock.PrevByOffset, originalBlock.Offset, prefixSize, true) };
        if (mBlocks[prefixIndex].NextByOffset != blockIndex) {
            mBlocks[prefixIndex].NextByOffset = blockIndex;
            mBlocks[blockIndex].PrevByOffset = prefixIndex;
        }
        InsertToBin(prefixIndex);
    }
    else {
        mBlocks[blockIndex].PrevByOffset = originalBlock.PrevByOffset;
    }

    if (suffixSize > 0) {
        int32_t suffixIndex{ InsertBlockAfter(blockIndex, alignedOffset + requestedSize, suffixSize, true) };
        InsertToBin(suffixIndex);
    }
    else {
        mBlocks[blockIndex].NextByOffset = originalBlock.NextByOffset;
        if (originalBlock.NextByOffset >= 0) {
            mBlocks[originalBlock.NextByOffset].PrevByOffset = blockIndex;
        }
    }

    if (mBlocks[blockIndex].PrevByOffset < 0) {
        mHeadByOffset = blockIndex;
    }
    return blockIndex;
}

int32_t GraphicsAllocator::MergeAdjacent(int32_t blockIndex) {
    int32_t currentIndex{ blockIndex };
    int32_t prevIndex{ mBlocks[currentIndex].PrevByOffset };

    if (prevIndex >= 0 && mBlocks[prevIndex].IsFree) {
        RemoveFromBin(prevIndex);
        SizeType currentSize{ mBlocks[currentIndex].Size };
        int32_t nextIndex{ mBlocks[currentIndex].NextByOffset };
        mBlocks[prevIndex].Size += currentSize;
        mBlocks[prevIndex].NextByOffset = nextIndex;

        if (nextIndex >= 0) {
            mBlocks[nextIndex].PrevByOffset = prevIndex;
        }

        mBlocks[currentIndex].Size = 0;
        mBlocks[currentIndex].IsFree = false;
        mBlocks[currentIndex].PrevByOffset = -1;
        mBlocks[currentIndex].NextByOffset = -1;
        currentIndex = prevIndex;
    }

    int32_t nextIndex{ mBlocks[currentIndex].NextByOffset };
    if (nextIndex >= 0 && mBlocks[nextIndex].IsFree) {
        RemoveFromBin(nextIndex);
        mBlocks[currentIndex].Size += mBlocks[nextIndex].Size;
        mBlocks[currentIndex].NextByOffset = mBlocks[nextIndex].NextByOffset;
        if (mBlocks[nextIndex].NextByOffset >= 0) {
            mBlocks[mBlocks[nextIndex].NextByOffset].PrevByOffset = currentIndex;
        }
        mBlocks[nextIndex].Size = 0;
        mBlocks[nextIndex].IsFree = false;
        mBlocks[nextIndex].PrevByOffset = -1;
        mBlocks[nextIndex].NextByOffset = -1;
    }

    if (mBlocks[currentIndex].PrevByOffset < 0) {
        mHeadByOffset = currentIndex;
    }

    return currentIndex;
}
