#pragma once

#include "Common.h"

namespace asset {
    class DataList final {
    public:
        DataList() = default;
        ~DataList();

        DataList(const DataList&) = delete;
        DataList& operator=(const DataList&) = delete;
        DataList(DataList&& Other) noexcept;
        DataList& operator=(DataList&& Other) noexcept;

    public:
        void Clear();
        void Reset();

        std::size_t SizeBytes() const;
        std::size_t Count() const;
        std::size_t ElementSize() const;
        std::size_t ElementAlignment() const;
        bool Empty() const;

        template <class T>
        void Reserve(std::size_t ElementCapacity);

        template <class T>
        void PushBack(const T& Value);

        template <class T>
        void Append(std::span<const T> Values);

        template <class T>
        std::span<T> Data();

        template <class T>
        std::span<const T> Data() const;

        std::span<std::byte> Bytes();
        std::span<const std::byte> Bytes() const;

    private:
        void Reallocate(std::size_t NewCapacity);
        void MoveFrom(DataList&& Other) noexcept;

        template <class T>
        void EnsureType() const;

        template <class T>
        void EnsureType();

    private:
        void* mData{ nullptr };
        std::size_t mCount{ 0 };
        std::size_t mCapacity{ 0 };
        std::size_t mElementSize{ 0 };
        std::size_t mAlignment{ alignof(std::max_align_t) };
    };

    template <class T>
    void DataList::Reserve(std::size_t ElementCapacity) {
        EnsureType<T>();
        if (ElementCapacity <= mCapacity) {
            return;
        }
        Reallocate(ElementCapacity);
    }

    template <class T>
    void DataList::PushBack(const T& Value) {
        EnsureType<T>();
        if (mCount + 1 > mCapacity) {
            const std::size_t NewCapacity{ (mCapacity == 0) ? 64 : (mCapacity * 2) };
            Reallocate(NewCapacity);
        }
        T* Dst{ reinterpret_cast<T*>(mData) };
        Dst[mCount] = Value;
        mCount += 1;
    }

    template <class T>
    void DataList::Append(std::span<const T> Values) {
        EnsureType<T>();
        const std::size_t Needed{ mCount + Values.size() };
        if (Needed > mCapacity) {
            std::size_t NewCapacity{ (mCapacity == 0) ? 64 : mCapacity };
            while (NewCapacity < Needed) {
                NewCapacity *= 2;
            }
            Reallocate(NewCapacity);
        }
        T* Dst{ reinterpret_cast<T*>(mData) };
        std::memcpy(Dst + mCount, Values.data(), Values.size_bytes());
        mCount += Values.size();
    }

    template <class T>
    std::span<T> DataList::Data() {
        EnsureType<T>();
        return std::span<T>{ reinterpret_cast<T*>(mData), mCount };
    }

    template <class T>
    std::span<const T> DataList::Data() const {
        EnsureType<T>();
        return std::span<const T>{ reinterpret_cast<const T*>(mData), mCount };
    }

    template <class T>
    void DataList::EnsureType() const {
        static_assert(std::is_trivially_copyable_v<T>, "DataList expects trivially copyable types (POD-like).");
        if (mElementSize == 0) {
            return;
        }
        if (mElementSize != sizeof(T) || mAlignment != alignof(T)) {
            throw AssetError{ "DataList: type mismatch (element size/alignment differs)" };
        }
    }

    template <class T>
    void DataList::EnsureType() {
        static_assert(std::is_trivially_copyable_v<T>, "DataList expects trivially copyable types (POD-like).");
        if (mElementSize == 0) {
            mElementSize = sizeof(T);
            mAlignment = alignof(T);
        }
        else if (mElementSize != sizeof(T) || mAlignment != alignof(T)) {
            throw AssetError{ "DataList: type mismatch (element size/alignment differs)" };
        }
    }
}