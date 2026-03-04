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
            static constexpr uint32_t FlCount{ 32 };
            static constexpr uint32_t SlCount{ 8 };
            static constexpr uint32_t SlShift{ 3 };
            static constexpr uint32_t SlMask{ SlCount - 1 };

            struct FreeBlock {
                OffsetType Offset{};
                SizeType Size{};
                int32_t PrevByOffset{};
                int32_t NextByOffset{};
                int32_t PrevByBin{};
                int32_t NextByBin{};
                bool IsFree{};
            };

            struct HeapAllocationState {
                ComPtr<ID3D12Heap> Heap{};
                SizeType HeapSize{};
                SizeType UsedSize{};
                D3D12_HEAP_PROPERTIES HeapProperties{};
                D3D12_HEAP_FLAGS HeapFlags{ D3D12_HEAP_FLAG_NONE };
                std::array<uint32_t, FlCount> FlBitmap{};
                std::array<std::array<int32_t, SlCount>, FlCount> FreeBins{};
                std::vector<FreeBlock> Blocks{};
                int32_t HeadByOffset{ -1 };
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
            bool Initialize(ID3D12Device* Device, SizeType BufferHeapSize, SizeType TextureHeapSize, const D3D12_HEAP_PROPERTIES& BufferHeapProperties, const D3D12_HEAP_PROPERTIES& TextureHeapProperties, D3D12_HEAP_FLAGS BufferHeapFlags = D3D12_HEAP_FLAG_NONE, D3D12_HEAP_FLAGS TextureHeapFlags = D3D12_HEAP_FLAG_NONE) override;
            void Reset() override;

            bool CanAllocate(const D3D12_RESOURCE_DESC& ResourceDesc) const override;
            std::unique_ptr<Interface::IAllocationHandle> AllocatePlacedResource(const D3D12_RESOURCE_DESC& ResourceDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* OptimizedClearValue = nullptr) override;
            std::unique_ptr<Interface::IAllocationHandle> AllocatePlacedResource(Interface::GraphicsAllocationType AllocationType, const D3D12_RESOURCE_DESC& ResourceDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* OptimizedClearValue = nullptr) override;

            AllocationHandle AllocatePlacedResourceHandle(const D3D12_RESOURCE_DESC& ResourceDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* OptimizedClearValue = nullptr);
            AllocationHandle AllocatePlacedResourceHandle(Interface::GraphicsAllocationType AllocationType, const D3D12_RESOURCE_DESC& ResourceDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* OptimizedClearValue = nullptr);

            ID3D12Heap* GetHeap() const override;
            ID3D12Heap* GetHeap(Interface::GraphicsAllocationType AllocationType) const override;
            SizeType GetHeapSize() const override;
            SizeType GetHeapSize(Interface::GraphicsAllocationType AllocationType) const override;
            SizeType GetUsedSize() const override;
            SizeType GetUsedSize(Interface::GraphicsAllocationType AllocationType) const override;

        private:
            static OffsetType AlignUp(OffsetType Value, OffsetType Alignment);
            static Interface::GraphicsAllocationType ResolveAllocationType(const D3D12_RESOURCE_DESC& ResourceDesc);
            static bool CanAllocateByType(Interface::GraphicsAllocationType AllocationType, const D3D12_RESOURCE_DESC& ResourceDesc);
            void ResetState(HeapAllocationState& State);
            bool InitializeState(HeapAllocationState& State, SizeType HeapSize, const D3D12_HEAP_PROPERTIES& HeapProperties, D3D12_HEAP_FLAGS HeapFlags);
            void FreeAllocation(Interface::GraphicsAllocationType AllocationType, OffsetType Offset, SizeType Size);
            void FreeAllocation(HeapAllocationState& State, OffsetType Offset, SizeType Size);
            uint32_t MappingInsert(SizeType Size, uint32_t& FirstLevel, uint32_t& SecondLevel) const;
            void InsertToBin(HeapAllocationState& State, int32_t BlockIndex);
            void RemoveFromBin(HeapAllocationState& State, int32_t BlockIndex);
            int32_t FindSuitableBlock(const HeapAllocationState& State, SizeType Size, SizeType Alignment) const;
            int32_t CreateBlock(HeapAllocationState& State, OffsetType Offset, SizeType Size, bool IsFree);
            int32_t FindBlockByOffset(const HeapAllocationState& State, OffsetType Offset) const;
            int32_t InsertBlockAfter(HeapAllocationState& State, int32_t PrevBlockIndex, OffsetType Offset, SizeType Size, bool IsFree);
            int32_t SplitBlockWithAlignment(HeapAllocationState& State, int32_t BlockIndex, SizeType RequestedSize, SizeType Alignment);
            int32_t MergeAdjacent(HeapAllocationState& State, int32_t BlockIndex);
            HeapAllocationState& GetState(Interface::GraphicsAllocationType AllocationType);
            const HeapAllocationState& GetState(Interface::GraphicsAllocationType AllocationType) const;

        private:
            ID3D12Device* mDevice{};
            HeapAllocationState mBufferState{};
            HeapAllocationState mTextureState{};
        };
    }
}
