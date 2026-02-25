#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include "Core/Common.h"
#include "Utility/DirectXInclude.h"
#include "AllocationHandle.h"

namespace Core {
    namespace DX {
        class GraphicsAllocator final : public Interface::IGraphicsAllocator {
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
            GraphicsAllocator(ID3D12Device* Device, SizeType HeapSize, const D3D12_HEAP_PROPERTIES& HeapProperties, D3D12_HEAP_FLAGS HeapFlags = D3D12_HEAP_FLAG_NONE);
            ~GraphicsAllocator();
            GraphicsAllocator(const GraphicsAllocator& Other) = delete;
            GraphicsAllocator& operator=(const GraphicsAllocator& Other) = delete;
            GraphicsAllocator(GraphicsAllocator&& Other) noexcept;
            GraphicsAllocator& operator=(GraphicsAllocator&& Other) noexcept;

        public:
            bool Initialize(ID3D12Device* Device, SizeType HeapSize, const D3D12_HEAP_PROPERTIES& HeapProperties, D3D12_HEAP_FLAGS HeapFlags = D3D12_HEAP_FLAG_NONE) override;
            void Reset() override;

            bool CanAllocate(const D3D12_RESOURCE_DESC& ResourceDesc) const override;
            std::unique_ptr<Interface::IAllocationHandle> AllocatePlacedResource(const D3D12_RESOURCE_DESC& ResourceDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* OptimizedClearValue = nullptr) override;

            AllocationHandle AllocatePlacedResourceHandle(const D3D12_RESOURCE_DESC& ResourceDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* OptimizedClearValue = nullptr);

            ID3D12Heap* GetHeap() const override;
            SizeType GetHeapSize() const override;
            SizeType GetUsedSize() const override;

        private:
            static OffsetType AlignUp(OffsetType Value, OffsetType Alignment);
            void FreeAllocation(OffsetType Offset, SizeType Size);
            uint32_t MappingInsert(SizeType Size, uint32_t& FirstLevel, uint32_t& SecondLevel) const;
            void InsertToBin(int32_t BlockIndex);
            void RemoveFromBin(int32_t BlockIndex);
            int32_t FindSuitableBlock(SizeType Size, SizeType Alignment) const;
            int32_t CreateBlock(OffsetType Offset, SizeType Size, bool IsFree);
            int32_t FindBlockByOffset(OffsetType Offset) const;
            int32_t InsertBlockAfter(int32_t PrevBlockIndex, OffsetType Offset, SizeType Size, bool IsFree);
            int32_t SplitBlockWithAlignment(int32_t BlockIndex, SizeType RequestedSize, SizeType Alignment);
            int32_t MergeAdjacent(int32_t BlockIndex);

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
