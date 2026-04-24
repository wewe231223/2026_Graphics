#pragma once

/*
PhysicsLib Header Guide
Role:
- Provides a fixed-capacity single-producer single-consumer ring queue for PhysicsRuntime commands.
Initialization:
- Default construction creates an empty queue with Capacity usable command slots.
Usage:
- Call TryEnqueue from the producer thread and TryDequeue from the consumer thread.
Notes:
- This queue is lock-free for the intended one-producer and one-consumer ownership model only.
*/

#include <array>
#include <atomic>
#include <cstddef>

template <typename Type, std::size_t Capacity>
class SpscRingQueue final {
public:
    SpscRingQueue();
    ~SpscRingQueue() = default;
    SpscRingQueue(const SpscRingQueue& Other) = delete;
    SpscRingQueue& operator=(const SpscRingQueue& Other) = delete;
    SpscRingQueue(SpscRingQueue&& Other) noexcept = delete;
    SpscRingQueue& operator=(SpscRingQueue&& Other) noexcept = delete;

public:
    bool TryEnqueue(const Type& Value);
    bool TryDequeue(Type& OutValue);

private:
    static constexpr std::size_t BufferSize{ Capacity + 1U };

private:
    std::array<Type, BufferSize> mBuffer;
    std::atomic<std::size_t> mHead;
    std::atomic<std::size_t> mTail;
};

template <typename Type, std::size_t Capacity>
SpscRingQueue<Type, Capacity>::SpscRingQueue()
    : mBuffer{},
      mHead{},
      mTail{} {
}

template <typename Type, std::size_t Capacity>
bool SpscRingQueue<Type, Capacity>::TryEnqueue(const Type& Value) {
    std::size_t Head{ mHead.load(std::memory_order_relaxed) };
    std::size_t NextHead{ (Head + 1U) % BufferSize };
    if (NextHead == mTail.load(std::memory_order_acquire)) {
        return false;
    }

    mBuffer[Head] = Value;
    mHead.store(NextHead, std::memory_order_release);
    return true;
}

template <typename Type, std::size_t Capacity>
bool SpscRingQueue<Type, Capacity>::TryDequeue(Type& OutValue) {
    std::size_t Tail{ mTail.load(std::memory_order_relaxed) };
    if (Tail == mHead.load(std::memory_order_acquire)) {
        return false;
    }

    OutValue = mBuffer[Tail];
    std::size_t NextTail{ (Tail + 1U) % BufferSize };
    mTail.store(NextTail, std::memory_order_release);
    return true;
}
