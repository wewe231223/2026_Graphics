#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include <wrl/client.h>
#include <d3d12.h>

namespace Interface {
    class IAllocationHandle {
    public:
        virtual ~IAllocationHandle() = default;

    public:
        virtual void Reset() = 0;
        virtual bool IsValid() const = 0;
        virtual ID3D12Resource* GetResource() const = 0;
        virtual std::uint64_t GetOffset() const = 0;
        virtual std::uint64_t GetSize() const = 0;
    };

    class IGraphicsAllocator {
    public:
        virtual ~IGraphicsAllocator() = default;

    public:
        virtual bool Initialize(ID3D12Device* Device, std::uint64_t HeapSize, const D3D12_HEAP_PROPERTIES& HeapProperties, D3D12_HEAP_FLAGS HeapFlags = D3D12_HEAP_FLAG_NONE) = 0;
        virtual void Reset() = 0;

        virtual bool CanAllocate(const D3D12_RESOURCE_DESC& ResourceDesc) const = 0;
        virtual std::unique_ptr<IAllocationHandle> AllocatePlacedResource(const D3D12_RESOURCE_DESC& ResourceDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* OptimizedClearValue = nullptr) = 0;

        virtual ID3D12Heap* GetHeap() const = 0;
        virtual std::uint64_t GetHeapSize() const = 0;
        virtual std::uint64_t GetUsedSize() const = 0;
    };

    struct CopyQueueCopyRequest final {
        Microsoft::WRL::ComPtr<ID3D12Resource> DestinationDefaultResource{};
        std::uint64_t DestinationOffset{};
        std::vector<std::byte> SourceData{};
    };

    class ICopyQueue {
    public:
        virtual ~ICopyQueue() = default;

    public:
        virtual bool Initialize(ID3D12Device* Device) = 0;
        virtual std::uint64_t EnqueueCopy(const CopyQueueCopyRequest& CopyRequest) = 0;
        virtual std::uint64_t EnqueueCopy(std::span<const CopyQueueCopyRequest> CopyRequests) = 0;

        virtual void DispatchCopies() = 0;
        virtual bool IsFenceComplete(std::uint64_t FenceValue) const = 0;
        virtual void WaitForFence(std::uint64_t FenceValue) const = 0;
        virtual void Flush() = 0;

        virtual std::uint64_t GetRequiredUploadBufferSize() const = 0;
    };
}


namespace Core {
    namespace DX {
        struct DrawRecordGPU {
            std::uint32_t ObjectIndex{ 0 };
            std::uint32_t MaterialIndex{ 0 };
            std::uint32_t Flags{ 0 };
            std::uint32_t Pad0{ 0 };
        };
    }
}
