#include "Memory.h"

using namespace Arche;

ChunkAllocator& ChunkAllocator::Instance() {
    static ChunkAllocator instance;
    return instance;
}

void* ChunkAllocator::Allocate() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mFreeChunks.empty()) {
        void* ptr = mFreeChunks.back();
        mFreeChunks.pop_back();
        return ptr;
    }
#if defined(_MSC_VER)
    return _aligned_malloc(CHUNK_SIZE, 64);
#else
    return std::aligned_alloc(64, CHUNK_SIZE);
#endif
}

void ChunkAllocator::Deallocate(void* ptr) {
    std::lock_guard<std::mutex> lock(mMutex);
    mFreeChunks.push_back(ptr);
}
