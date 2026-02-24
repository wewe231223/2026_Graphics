#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "Utility/DirectXInclude.h"
#include "Core/DX/AllocationHandle.h"
#include "Core/DX/GraphicsAllocator.h"
#include "Core/DX/CopyQueue.h"

namespace Core {
    namespace DX {
        class GraphicsVector {
        public:
            using ValueType = std::byte;
            using SizeType = std::size_t;

        public:
            GraphicsVector();
            GraphicsVector(GraphicsAllocator& graphicsAllocator, SizeType initialSizeInBytes, D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COPY_DEST);
            ~GraphicsVector();
            GraphicsVector(const GraphicsVector& other) = delete;
            GraphicsVector& operator=(const GraphicsVector& other) = delete;
            GraphicsVector(GraphicsVector&& other) noexcept;
            GraphicsVector& operator=(GraphicsVector&& other) noexcept;

        public:
            bool Initialize(GraphicsAllocator& graphicsAllocator, SizeType initialSizeInBytes, D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COPY_DEST);
            bool Copy(GraphicsAllocator& graphicsAllocator, void* sourceData, SizeType copySizeInBytes);
            void Reset();

            CopyQueueCopyRequest CreateCopyQueueCopyRequest(GraphicsAllocator& graphicsAllocator, UINT64 destinationOffset = 0);

            void CreateShaderResourceView(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle, DXGI_FORMAT format, UINT firstElement, UINT numElements, UINT structureByteStride, D3D12_BUFFER_SRV_FLAGS bufferFlags) const;

            bool IsValid() const;
            ID3D12Resource* GetResource() const;
            const AllocationHandle& GetAllocationHandle() const;
            D3D12_RESOURCE_STATES GetResourceState() const;

            D3D12_RESOURCE_BARRIER CreateTransitionBarrier(D3D12_RESOURCE_STATES nextState);
            SizeType GetSizeInBytes() const;
            SizeType GetCapacityInBytes() const;

        private:
            static constexpr SizeType AllocationUnitSizeInBytes{ 64ull * 1024ull };

            static SizeType AlignCapacity(SizeType sizeInBytes);
            bool Reallocate(GraphicsAllocator& graphicsAllocator, SizeType requiredSizeInBytes);
            void TryShrink(GraphicsAllocator& graphicsAllocator);

        private:
            AllocationHandle mAllocationHandle{};
            std::vector<ValueType> mUploadData{};

            SizeType mSizeInBytes{};
            SizeType mCapacityInBytes{};

            D3D12_RESOURCE_FLAGS mResourceFlags{};
            D3D12_RESOURCE_STATES mResourceState{};

            uint64_t mCopyRequestCreationCount{};
        };
    }
}
