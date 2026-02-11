#pragma once
#include <vector>
#include <mutex>
#include <cstdlib>
#include <new>
#include "Common.h"

namespace Arche {

    class ChunkAllocator {
    public:
        ChunkAllocator() = default;
        ~ChunkAllocator() = default;

        ChunkAllocator(const ChunkAllocator&) = delete;
        ChunkAllocator& operator=(const ChunkAllocator&) = delete;

        ChunkAllocator(ChunkAllocator&&) = delete;
        ChunkAllocator& operator=(ChunkAllocator&&) = delete;

    public:
        static ChunkAllocator& Instance();
        void* Allocate();
        void Deallocate(void* ptr);

    private:
        std::vector<void*> mFreeChunks{};
        std::mutex mMutex{};
    };
}
