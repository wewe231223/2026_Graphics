#pragma once
#include <cstddef>
#include <memory>
#include <vector>
#include <iterator>
#include "Utility/DirectXInclude.h"
#include "Core/DX/AllocationHandle.h"
#include "Core/DX/GraphicsAllocator.h"
#include "Core/DX/CopyQueue.h"

namespace Core {
    namespace DX {
        class GraphicsBuffer {
        public:
            using ValueType = std::byte;
            using Pointer = ValueType*;
            using ConstPointer = const ValueType*;
            using Reference = ValueType&;
            using ConstReference = const ValueType&;
            using Iterator = ValueType*;
            using ConstIterator = const ValueType*;
            using ReverseIterator = std::reverse_iterator<Iterator>;
            using ConstReverseIterator = std::reverse_iterator<ConstIterator>;
            using SizeType = std::size_t;

        public:
            GraphicsBuffer();
            GraphicsBuffer(ID3D12Device* device);
            GraphicsBuffer(ID3D12Device* device, SizeType sizeInBytes, D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COPY_DEST);
            ~GraphicsBuffer();
            GraphicsBuffer(const GraphicsBuffer& other) = delete;
            GraphicsBuffer& operator=(const GraphicsBuffer& other) = delete;
            GraphicsBuffer(GraphicsBuffer&& other) noexcept;
            GraphicsBuffer& operator=(GraphicsBuffer&& other) noexcept;

        public:
            bool Initialize(ID3D12Device* device, SizeType sizeInBytes, D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COPY_DEST);
            void Reset();

            Iterator begin();
            ConstIterator begin() const;
            ConstIterator cbegin() const;
            Iterator end();
            ConstIterator end() const;
            ConstIterator cend() const;
            ReverseIterator rbegin();
            ConstReverseIterator rbegin() const;
            ConstReverseIterator crbegin() const;
            ReverseIterator rend();
            ConstReverseIterator rend() const;
            ConstReverseIterator crend() const;

            Reference operator[](SizeType index);
            ConstReference operator[](SizeType index) const;
            Reference front();
            ConstReference front() const;
            Reference back();
            ConstReference back() const;
            Pointer data();
            ConstPointer data() const;

            bool empty() const;
            SizeType size() const;
            SizeType max_size() const;
            void fill(ValueType value);
            void swap(GraphicsBuffer& other) noexcept;

            bool IsValid() const;
            ID3D12Resource* GetResource() const;
            const AllocationHandle& GetAllocationHandle() const;
            D3D12_RESOURCE_STATES GetResourceState() const;
            D3D12_RESOURCE_BARRIER CreateTransitionBarrier(D3D12_RESOURCE_STATES nextState);
            CopyQueueCopyRequest CreateCopyRequest(UINT64 destinationOffset = 0) const;

        private:
            GraphicsAllocator mAllocator{};
            AllocationHandle mAllocationHandle{};
            std::unique_ptr<ValueType[]> mUploadData{};
            SizeType mSizeInBytes{};
            D3D12_RESOURCE_STATES mResourceState{};
        };
    }
}
