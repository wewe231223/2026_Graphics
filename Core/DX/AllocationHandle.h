#pragma once
#include "Core/Common.h"
#include "Utility/DirectXInclude.h"

namespace Core {
    namespace DX {
        class GraphicsAllocator;

        class AllocationHandle final : public Interface::IAllocationHandle {
            friend GraphicsAllocator;

        public:
            using OffsetType = uint64_t;
            using SizeType = uint64_t;

        public:
            AllocationHandle();
            ~AllocationHandle();
            AllocationHandle(const AllocationHandle& Other) = delete;
            AllocationHandle& operator=(const AllocationHandle& Other) = delete;
            AllocationHandle(AllocationHandle&& Other) noexcept;
            AllocationHandle& operator=(AllocationHandle&& Other) noexcept;

        public:
            void Reset() override;
            bool IsValid() const override;
            ID3D12Resource* GetResource() const override;
            OffsetType GetOffset() const override;
            SizeType GetSize() const override;

        public:
            const ComPtr<ID3D12Resource>& GetResourceComPtr() const;

        private:
            AllocationHandle(GraphicsAllocator* Allocator, ComPtr<ID3D12Resource>&& Resource, OffsetType Offset, SizeType Size);

        private:
            GraphicsAllocator* mAllocator{};
            ComPtr<ID3D12Resource> mResource{};
            OffsetType mOffset{};
            SizeType mSize{};
        };
    }
}
