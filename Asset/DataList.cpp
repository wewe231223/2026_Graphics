#include "DataList.h"

using namespace asset;

DataList::~DataList() {
    Reset();
}

DataList::DataList(DataList&& Other) noexcept
    : mData{ Other.mData },
    mCount{ Other.mCount },
    mCapacity{ Other.mCapacity },
    mElementSize{ Other.mElementSize },
    mAlignment{ Other.mAlignment } {
    Other.mData = nullptr;
    Other.mCount = 0;
    Other.mCapacity = 0;
    Other.mElementSize = 0;
    Other.mAlignment = alignof(std::max_align_t);
}

DataList& DataList::operator=(DataList&& Other) noexcept {
    if (this != &Other) {
        Reset();
        MoveFrom(std::move(Other));
    }
    return *this;
}

void DataList::Clear() {
    mCount = 0;
}

void DataList::Reset() {
    if (mData != nullptr) {
        ::operator delete(mData, std::align_val_t{ mAlignment });
        mData = nullptr;
    }
    mCount = 0;
    mCapacity = 0;
    mElementSize = 0;
    mAlignment = alignof(std::max_align_t);
}

std::size_t DataList::SizeBytes() const {
    return mCount * mElementSize;
}

std::size_t DataList::Count() const {
    return mCount;
}

std::size_t DataList::ElementSize() const {
    return mElementSize;
}

std::size_t DataList::ElementAlignment() const {
    return mAlignment;
}

bool DataList::Empty() const {
    return mCount == 0;
}

std::span<std::byte> DataList::Bytes() {
    return std::span<std::byte>{ reinterpret_cast<std::byte*>(mData), SizeBytes() };
}

std::span<const std::byte> DataList::Bytes() const {
    return std::span<const std::byte>{ reinterpret_cast<const std::byte*>(mData), SizeBytes() };
}

void DataList::Reallocate(std::size_t NewCapacity) {
    assert(mElementSize != 0);
    const std::size_t NewBytes{ NewCapacity * mElementSize };
    void* NewData{ ::operator new(NewBytes, std::align_val_t{ mAlignment }) };
    if (mData != nullptr) {
        std::memcpy(NewData, mData, mCount * mElementSize);
        ::operator delete(mData, std::align_val_t{ mAlignment });
    }
    mData = NewData;
    mCapacity = NewCapacity;
}

void DataList::MoveFrom(DataList&& Other) noexcept {
    mData = Other.mData;
    mCount = Other.mCount;
    mCapacity = Other.mCapacity;
    mElementSize = Other.mElementSize;
    mAlignment = Other.mAlignment;

    Other.mData = nullptr;
    Other.mCount = 0;
    Other.mCapacity = 0;
    Other.mElementSize = 0;
    Other.mAlignment = alignof(std::max_align_t);
}