#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <d3d12.h>

namespace Interface {
    class IFutureSyncObject {
    public:
        virtual ~IFutureSyncObject() = default;

    public:
        virtual bool IsFutureComplete(std::uint64_t FutureTicket) const = 0;
        virtual void WaitFuture(std::uint64_t FutureTicket) const = 0;
        virtual void QueueWaitFuture(ID3D12CommandQueue* WaitingQueue, std::uint64_t FutureTicket) const = 0;
    };

    class Future final {
    private:
        struct FuturePoint final {
            const IFutureSyncObject* mSyncObject{};
            std::uint64_t mTicket{};
        };

    public:
        Future();
        Future(const IFutureSyncObject* SyncObject, std::uint64_t Ticket);
        ~Future();
        Future(const Future& Other);
        Future& operator=(const Future& Other);
        Future(Future&& Other) noexcept;
        Future& operator=(Future&& Other) noexcept;

    public:
        static Future Merge(std::span<const Future> Futures);

        bool IsValid() const;
        bool IsComplete() const;
        bool IsInFlight() const;
        void Wait() const;
        void QueueWait(ID3D12CommandQueue* WaitingQueue) const;

    private:
        bool Contains(const IFutureSyncObject* SyncObject, std::uint64_t Ticket) const;

    private:
        std::vector<FuturePoint> mFuturePoints{};
    };
}
