#include "AllocationHandle.h"
#include "GraphicsAllocator.h"

using namespace Core::DX;

AllocationHandle::AllocationHandle()
    : mAllocator{},
    mResource{},
    mOffset{},
    mSize{} {
}

AllocationHandle::AllocationHandle(GraphicsAllocator* allocator, ComPtr<ID3D12Resource>&& resource, OffsetType offset, SizeType size)
    : mAllocator{ allocator },

    mResource{ std::move(resource) },
    mOffset{ offset },
    mSize{ size } {
}

AllocationHandle::~AllocationHandle() {
    Reset();
}

AllocationHandle::AllocationHandle(AllocationHandle&& other) noexcept
    : mAllocator{ other.mAllocator },
    mResource{ std::move(other.mResource) },
    mOffset{ other.mOffset },
    mSize{ other.mSize } {

    other.mAllocator = nullptr;
    other.mOffset = 0;
    other.mSize = 0;
}

AllocationHandle& AllocationHandle::operator=(AllocationHandle&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    Reset();
    mAllocator = other.mAllocator;
    mResource = std::move(other.mResource);
    mOffset = other.mOffset;
    mSize = other.mSize;
    other.mAllocator = nullptr;
    other.mOffset = 0;
    other.mSize = 0;
    return *this;
}

void AllocationHandle::Reset() {
    ComPtr<ID3D12Resource> releasedResource{ std::move(mResource) };
    if (releasedResource != nullptr) {
        releasedResource.Reset();
    }

    if (mAllocator != nullptr && mSize > 0) {
        mAllocator->FreeAllocation(mOffset, mSize);
    }

    mAllocator = nullptr;
    mOffset = 0;
    mSize = 0;
}

bool AllocationHandle::IsValid() const {
    return mResource != nullptr && mSize > 0;
}

ID3D12Resource* AllocationHandle::GetResource() const {
    return mResource.Get();
}

AllocationHandle::OffsetType AllocationHandle::GetOffset() const {
    return mOffset;
}

AllocationHandle::SizeType AllocationHandle::GetSize() const {
    return mSize;
}

