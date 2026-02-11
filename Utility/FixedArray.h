#include <iostream>
#include <memory>
#include <stdexcept>
#include <iterator>
#include <cstddef>
#include <compare>
#include <utility>
#include "Utility/ErrorHandler.h"

#ifdef max 
#undef max
#endif 

namespace Cont {
    template <typename T>
    class FixedArray {
    public:
        struct Iterator {
        public:
            using iterator_concept = std::contiguous_iterator_tag;
            using iterator_category = std::random_access_iterator_tag;
            using value_type = T;
            using difference_type = std::ptrdiff_t;
            using pointer = T*;
            using reference = T&;

            friend class FixedArray;
            friend struct ConstIterator;

            Iterator() : mPtr(nullptr) {
            }

            explicit Iterator(pointer ptr) : mPtr(ptr) {
            }

            ~Iterator() = default;

            Iterator(const Iterator&) = default;
            Iterator(Iterator&&) = default;

            Iterator& operator=(const Iterator&) = default;
            Iterator& operator=(Iterator&&) = default;

            reference operator*() const {
                return *mPtr;
            }

            pointer operator->() const {
                return mPtr;
            }

            Iterator& operator++() {
                ++mPtr;
                return *this;
            }

            Iterator operator++(int) {
                Iterator tmp = *this;
                ++mPtr;
                return tmp;
            }

            Iterator& operator--() {
                --mPtr;
                return *this;
            }

            Iterator operator--(int) {
                Iterator tmp = *this;
                --mPtr;
                return tmp;
            }

            Iterator& operator+=(difference_type n) {
                mPtr += n;
                return *this;
            }

            Iterator& operator-=(difference_type n) {
                mPtr -= n;
                return *this;
            }

            reference operator[](difference_type n) const {
                return mPtr[n];
            }

            auto operator<=>(const Iterator&) const = default;

            friend Iterator operator+(Iterator it, difference_type n) {
                return Iterator(it.mPtr + n);
            }

            friend Iterator operator+(difference_type n, Iterator it) {
                return Iterator(it.mPtr + n);
            }

            friend Iterator operator-(Iterator it, difference_type n) {
                return Iterator(it.mPtr - n);
            }

            friend difference_type operator-(const Iterator& a, const Iterator& b) {
                return a.mPtr - b.mPtr;
            }

        private:
            pointer mPtr;
        };

        struct ConstIterator {
        public:
            using iterator_concept = std::contiguous_iterator_tag;
            using iterator_category = std::random_access_iterator_tag;
            using value_type = T;
            using difference_type = std::ptrdiff_t;
            using pointer = const T*;
            using reference = const T&;

            ConstIterator() : mPtr(nullptr) {
            }

            explicit ConstIterator(pointer ptr) : mPtr(ptr) {
            }

            ConstIterator(const Iterator& it) : mPtr(it.m_ptr) {
            }

            ~ConstIterator() = default;

            ConstIterator(const ConstIterator&) = default;
            ConstIterator(ConstIterator&&) = default;

            ConstIterator& operator=(const ConstIterator&) = default;
            ConstIterator& operator=(ConstIterator&&) = default;

            reference operator*() const {
                return *mPtr;
            }

            pointer operator->() const {
                return mPtr;
            }

            ConstIterator& operator++() {
                ++mPtr;
                return *this;
            }

            ConstIterator operator++(int) {
                ConstIterator tmp = *this;
                ++mPtr;
                return tmp;
            }

            ConstIterator& operator--() {
                --mPtr;
                return *this;
            }

            ConstIterator operator--(int) {
                ConstIterator tmp = *this;
                --mPtr;
                return tmp;
            }

            ConstIterator& operator+=(difference_type n) {
                mPtr += n;
                return *this;
            }

            ConstIterator& operator-=(difference_type n) {
                mPtr -= n;
                return *this;
            }

            reference operator[](difference_type n) const {
                return mPtr[n];
            }

            auto operator<=>(const ConstIterator&) const = default;

            friend ConstIterator operator+(ConstIterator it, difference_type n) {
                return ConstIterator(it.mPtr + n);
            }

            friend ConstIterator operator+(difference_type n, ConstIterator it) {
                return ConstIterator(it.mPtr + n);
            }

            friend ConstIterator operator-(ConstIterator it, difference_type n) {
                return ConstIterator(it.mPtr - n);
            }

            friend difference_type operator-(const ConstIterator& a, const ConstIterator& b) {
                return a.mPtr - b.mPtr;
            }

        private:
            pointer mPtr;
        };

    public:
        using value_type = T;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using const_pointer = const T*;
        using reference = T&;
        using const_reference = const T&;
        using iterator = Iterator;
        using const_iterator = ConstIterator;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        constexpr FixedArray() noexcept = default;

        ~FixedArray() {
            if (mCapacity > 0 && mPtr) {
                T* ptr = reinterpret_cast<T*>(mPtr.get());
                for (size_type i = 0; i < mCapacity; ++i) {
                    std::destroy_at(ptr + (mCapacity - 1 - i));
                }
            }
        }

        FixedArray(const FixedArray&) = delete;
        FixedArray& operator=(const FixedArray&) = delete;

        FixedArray(FixedArray&&) noexcept = default;
        FixedArray& operator=(FixedArray&&) noexcept = default;

        T& operator[](size_type index) {
            return reinterpret_cast<T*>(mPtr.get())[index];
        }

        const T& operator[](size_type index) const {
            return reinterpret_cast<const T*>(mPtr.get())[index];
        }

    public:
        template <typename... Args>
        void Init(size_type size, Args&&... args) {
            if (mPtr) {
				ErrorHandler::report("FixedArray Initialization Error", "FixedArray is already initialized.", ErrorHandler::Level::Critical);
            }
            if (size == 0) {
                return;
            }

            mPtr = std::make_unique<std::byte[]>(size * sizeof(T));
            mCapacity = size;

            T* ptr = reinterpret_cast<T*>(mPtr.get());
            for (size_type i = 0; i < size; ++i) {
                std::construct_at(ptr + i, std::forward<Args>(args)...);
            }
        }

        T& at(size_type index) {
            if (index >= mCapacity) {
                throw std::out_of_range("Index out of range");
            }
            return (*this)[index];
        }

        const T& at(size_type index) const {
            if (index >= mCapacity) {
                throw std::out_of_range("Index out of range");
            }
            return (*this)[index];
        }

        T* data() noexcept {
            return reinterpret_cast<T*>(mPtr.get());
        }

        const T* data() const noexcept {
            return reinterpret_cast<const T*>(mPtr.get());
        }

        iterator begin() noexcept {
            return iterator(data());
        }

        const_iterator begin() const noexcept {
            return const_iterator(data());
        }

        const_iterator cbegin() const noexcept {
            return begin();
        }

        iterator end() noexcept {
            return iterator(data() + mCapacity);
        }

        const_iterator end() const noexcept {
            return const_iterator(data() + mCapacity);
        }

        const_iterator cend() const noexcept {
            return end();
        }

        reverse_iterator rbegin() noexcept {
            return reverse_iterator(end());
        }

        const_reverse_iterator rbegin() const noexcept {
            return const_reverse_iterator(end());
        }

        reverse_iterator rend() noexcept {
            return reverse_iterator(begin());
        }

        const_reverse_iterator rend() const noexcept {
            return const_reverse_iterator(begin());
        }

        [[nodiscard]] size_type size() const noexcept {
            return mCapacity;
        }

        [[nodiscard]] bool empty() const noexcept {
            return mCapacity == 0;
        }

        [[nodiscard]] size_type max_size() const noexcept {
            return std::numeric_limits<size_type>::max() / sizeof(T);
        }

    private:
        std::unique_ptr<std::byte[]> mPtr;
        size_type mCapacity{ 0 };
    };
}
