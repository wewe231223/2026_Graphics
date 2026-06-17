#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>
#include <wrl/client.h>
#include <d3d12.h>
#include "Core/Future.h"

namespace Core {
    namespace DX {
        class DescriptorHandle;
    }
}

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

    struct AllocatePlacedResourceParameters final {
        const D3D12_RESOURCE_DESC& ResourceDesc;
        D3D12_RESOURCE_STATES InitialState;
        const D3D12_CLEAR_VALUE* OptimizedClearValue;
        const wchar_t* ResourceName;
    };

    class IGraphicsAllocator {
    public:
        virtual ~IGraphicsAllocator() = default;

    public:
        virtual bool Initialize(ID3D12Device* Device, std::uint64_t HeapSize, const D3D12_HEAP_PROPERTIES& HeapProperties, D3D12_HEAP_FLAGS HeapFlags = D3D12_HEAP_FLAG_NONE) = 0;
        virtual void Reset() = 0;

        virtual bool CanAllocate(const D3D12_RESOURCE_DESC& ResourceDesc) const = 0;
        virtual std::unique_ptr<IAllocationHandle> AllocatePlacedResource(const AllocatePlacedResourceParameters& Parameters) = 0;

        virtual ID3D12Heap* GetHeap() const = 0;
        virtual std::uint64_t GetHeapSize() const = 0;
        virtual std::uint64_t GetUsedSize() const = 0;
    };

    struct CopyQueueCopyRequest final {
        Microsoft::WRL::ComPtr<ID3D12Resource> DestinationDefaultResource{};
        std::uint64_t DestinationOffset{};
        std::span<const std::byte> SourceData{};
    };

    struct CopyQueueTextureCopyRequest final {
        Microsoft::WRL::ComPtr<ID3D12Resource> DestinationTextureResource{};
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> SourceLayouts{};
        std::vector<std::byte> SourceData{};
    };

    using ComputeCommandRecorder = std::function<void(ID3D12GraphicsCommandList* CommandList)>;

    struct ComputeQueueDispatchRequest final {
        Future WaitFuture{};
        Microsoft::WRL::ComPtr<ID3D12RootSignature> RootSignature{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineState{};
        std::vector<ID3D12DescriptorHeap*> DescriptorHeaps{};
        ComputeCommandRecorder RecordCommands{};
        std::uint32_t ThreadGroupCountX{};
        std::uint32_t ThreadGroupCountY{ 1 };
        std::uint32_t ThreadGroupCountZ{ 1 };
    };


    class IDescriptorHeap {
    public:
        virtual ~IDescriptorHeap() = default;

    public:
        virtual Core::DX::DescriptorHandle Allocate() = 0;
        virtual ID3D12DescriptorHeap* GetHeap() const = 0;
    };

    class ICopyQueue : public IFutureSyncObject {
    public:
        virtual ~ICopyQueue() = default;

    public:
        virtual bool Initialize(ID3D12Device* Device) = 0;
        virtual Future EnqueueCopyFuture(const CopyQueueCopyRequest& CopyRequest) = 0;
        virtual Future EnqueueCopyFuture(std::span<const CopyQueueCopyRequest> CopyRequests) = 0;
        virtual Future EnqueueTextureCopyFuture(const CopyQueueTextureCopyRequest& CopyRequest) = 0;
        virtual Future EnqueueTextureCopyFuture(std::span<const CopyQueueTextureCopyRequest> CopyRequests) = 0;

        virtual void DispatchCopies() = 0;
        virtual void Flush() = 0;

        virtual std::uint64_t GetRequiredUploadBufferSize() const = 0;
    };

    class IComputeQueue : public IFutureSyncObject {
    public:
        virtual ~IComputeQueue() = default;

    public:
        virtual bool Initialize(ID3D12Device* Device) = 0;
        virtual Future EnqueueComputeFuture(const ComputeQueueDispatchRequest& DispatchRequest) = 0;
        virtual Future EnqueueComputeFuture(std::span<const ComputeQueueDispatchRequest> DispatchRequests) = 0;

        virtual void DispatchComputes() = 0;
        virtual void Flush() = 0;
        virtual ID3D12CommandQueue* GetCommandQueue() const = 0;
    };
}


namespace Core {
    namespace DX {
        struct DrawRecordGPU {
            std::uint32_t ObjectIndex{ 0 };
            std::uint32_t MaterialIndex{ 0 };
            std::uint32_t Flags{ 0 };
            std::uint32_t TerrainPatchContextIndex{ 0xffffffffu };
        };
    }
}
