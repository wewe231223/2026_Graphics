#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "Utility/DirectXInclude.h"
#include "AllocationHandle.h"


//가나다라마바사 
namespace Core {
    namespace DX {
        class GraphicsAllocator {
			friend AllocationHandle;
        public:
            using OffsetType = uint64_t;
            using SizeType = uint64_t; 
        private:
            static constexpr uint32_t FlCount = 32;
            static constexpr uint32_t SlCount = 8;
            static constexpr uint32_t SlShift = 3;
            static constexpr uint32_t SlMask = SlCount - 1;

            struct FreeBlock {
                OffsetType Offset{};
                SizeType Size{};
                int32_t PrevByOffset{};
                int32_t NextByOffset{};
                int32_t PrevByBin{};
                int32_t NextByBin{};
                bool IsFree{};
            };

        public:
            GraphicsAllocator();
            GraphicsAllocator(ID3D12Device* device, SizeType heapSize, const D3D12_HEAP_PROPERTIES& heapProperties, D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE);
            ~GraphicsAllocator();
            GraphicsAllocator(const GraphicsAllocator& other) = delete;
            GraphicsAllocator& operator=(const GraphicsAllocator& other) = delete;
            GraphicsAllocator(GraphicsAllocator&& other) noexcept;
            GraphicsAllocator& operator=(GraphicsAllocator&& other) noexcept;

        public:
            bool Initialize(ID3D12Device* device, SizeType heapSize, const D3D12_HEAP_PROPERTIES& heapProperties, D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE);
            void Reset();

            bool CanAllocate(const D3D12_RESOURCE_DESC& resourceDesc) const;
            AllocationHandle AllocatePlacedResource(const D3D12_RESOURCE_DESC& resourceDesc, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* optimizedClearValue = nullptr);

            ID3D12Heap* GetHeap() const;
            SizeType GetHeapSize() const;
            SizeType GetUsedSize() const;

        private:
            static OffsetType AlignUp(OffsetType value, OffsetType alignment);
            void FreeAllocation(OffsetType offset, SizeType size);
            uint32_t MappingInsert(SizeType size, uint32_t& firstLevel, uint32_t& secondLevel) const;
            void InsertToBin(int32_t blockIndex);
            void RemoveFromBin(int32_t blockIndex);
            int32_t FindSuitableBlock(SizeType size, SizeType alignment) const;
            int32_t CreateBlock(OffsetType offset, SizeType size, bool isFree);
            int32_t FindBlockByOffset(OffsetType offset) const;
            int32_t InsertBlockAfter(int32_t prevBlockIndex, OffsetType offset, SizeType size, bool isFree);
            int32_t SplitBlockWithAlignment(int32_t blockIndex, SizeType requestedSize, SizeType alignment);
            int32_t MergeAdjacent(int32_t blockIndex);

        private:
            ID3D12Device* mDevice{};
            ComPtr<ID3D12Heap> mHeap{};
            SizeType mHeapSize{};
            SizeType mUsedSize{};
            D3D12_HEAP_PROPERTIES mHeapProperties{};
            D3D12_HEAP_FLAGS mHeapFlags{};
            std::array<uint32_t, FlCount> mFlBitmap{};
            std::array<std::array<int32_t, SlCount>, FlCount> mFreeBins{};
            std::vector<FreeBlock> mBlocks{};
            int32_t mHeadByOffset{};
        };
    }
}
