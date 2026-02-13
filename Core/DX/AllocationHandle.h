#pragma once 
#include "Utility/DirectXInclude.h"

namespace Core{
    namespace DX {
        class GraphicsAllocator; 

        class AllocationHandle {
            friend GraphicsAllocator;

        public:
            using OffsetType = uint64_t;
            using SizeType = uint64_t;

        public:
            AllocationHandle();
            ~AllocationHandle();
            AllocationHandle(const AllocationHandle& other) = delete;
            AllocationHandle& operator=(const AllocationHandle& other) = delete;
            AllocationHandle(AllocationHandle&& other) noexcept;
            AllocationHandle& operator=(AllocationHandle&& other) noexcept;

        public:
            void Reset();
            bool IsValid() const;
            const ComPtr<ID3D12Resource>& GetResource() const;
            OffsetType GetOffset() const;
            SizeType GetSize() const;

        private:
            AllocationHandle(GraphicsAllocator* allocator, ComPtr<ID3D12Resource>&& resource, OffsetType offset, SizeType size);

        private:
            GraphicsAllocator* mAllocator{};
            ComPtr<ID3D12Resource> mResource{};
            OffsetType mOffset{};
            SizeType mSize{};
        };
    } 
}
