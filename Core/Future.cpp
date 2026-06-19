#include "Core/Future.h"
#include <cstddef>
#include <utility>

using namespace Interface;

void IFutureSyncObject::QueueWaitFutures(ID3D12CommandQueue* WaitingQueue, std::span<const std::uint64_t> FutureTickets) const {
    if (WaitingQueue == nullptr) {
        return;
    }

    for (std::uint64_t FutureTicket : FutureTickets) {
        QueueWaitFuture(WaitingQueue, FutureTicket);
    }
}
Future::Future()
    : mFuturePoints{} {
}

Future::Future(const IFutureSyncObject* SyncObject, std::uint64_t Ticket)
    : mFuturePoints{} {
    if (SyncObject != nullptr and Ticket > 0) {
        FuturePoint Point{};
        Point.mSyncObject = SyncObject;
        Point.mTicket = Ticket;
        mFuturePoints.push_back(Point);
    }
}

Future::~Future() {
}

Future::Future(const Future& Other)
    : mFuturePoints{ Other.mFuturePoints } {
}

Future& Future::operator=(const Future& Other) {
    if (this != &Other) {
        mFuturePoints = Other.mFuturePoints;
    }

    return *this;
}

Future::Future(Future&& Other) noexcept
    : mFuturePoints{ std::move(Other.mFuturePoints) } {
}

Future& Future::operator=(Future&& Other) noexcept {
    if (this != &Other) {
        mFuturePoints = std::move(Other.mFuturePoints);
    }

    return *this;
}

Future Future::Merge(std::span<const Future> Futures) {
    Future MergedFuture{};

    for (const Future& SourceFuture : Futures) {
        for (const FuturePoint& Point : SourceFuture.mFuturePoints) {
            if (MergedFuture.Contains(Point.mSyncObject, Point.mTicket) == false) {
                MergedFuture.mFuturePoints.push_back(Point);
            }
        }
    }

    return MergedFuture;
}

bool Future::IsValid() const {
    return mFuturePoints.empty() == false;
}

bool Future::IsComplete() const {
    if (IsValid() == false) {
        return true;
    }

    for (const FuturePoint& Point : mFuturePoints) {
        if (Point.mSyncObject == nullptr) {
            continue;
        }

        if (Point.mSyncObject->IsFutureComplete(Point.mTicket) == false) {
            return false;
        }
    }

    return true;
}

bool Future::IsInFlight() const {
    return IsComplete() == false;
}

void Future::Wait() const {
    for (const FuturePoint& Point : mFuturePoints) {
        if (Point.mSyncObject == nullptr) {
            continue;
        }

        Point.mSyncObject->WaitFuture(Point.mTicket);
    }
}

void Future::QueueWait(ID3D12CommandQueue* WaitingQueue) const {
    if (WaitingQueue == nullptr) {
        return;
    }

    std::vector<const IFutureSyncObject*> SyncObjects{};
    std::vector<std::vector<std::uint64_t>> FutureTicketsBySyncObject{};

    for (const FuturePoint& Point : mFuturePoints) {
        if (Point.mSyncObject == nullptr) {
            continue;
        }

        std::size_t SyncObjectIndex{};
        while (SyncObjectIndex < SyncObjects.size() and SyncObjects[SyncObjectIndex] != Point.mSyncObject) {
            SyncObjectIndex += 1;
        }

        if (SyncObjectIndex == SyncObjects.size()) {
            SyncObjects.push_back(Point.mSyncObject);
            FutureTicketsBySyncObject.emplace_back();
        }

        FutureTicketsBySyncObject[SyncObjectIndex].push_back(Point.mTicket);
    }

    for (std::size_t SyncObjectIndex{}; SyncObjectIndex < SyncObjects.size(); SyncObjectIndex += 1) {
        SyncObjects[SyncObjectIndex]->QueueWaitFutures(WaitingQueue, FutureTicketsBySyncObject[SyncObjectIndex]);
    }
}
bool Future::Contains(const IFutureSyncObject* SyncObject, std::uint64_t Ticket) const {
    for (const FuturePoint& Point : mFuturePoints) {
        if (Point.mSyncObject == SyncObject and Point.mTicket == Ticket) {
            return true;
        }
    }

    return false;
}
