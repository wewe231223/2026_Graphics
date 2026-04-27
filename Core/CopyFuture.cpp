#include "Core/Common.h"

using namespace Interface;

CopyFuture::CopyFuture()
    : mFuturePoints{} {
}

CopyFuture::~CopyFuture() {
}

CopyFuture::CopyFuture(const CopyFuture& Other)
    : mFuturePoints{ Other.mFuturePoints } {
}

CopyFuture& CopyFuture::operator=(const CopyFuture& Other) {
    if (this != &Other) {
        mFuturePoints = Other.mFuturePoints;
    }

    return *this;
}

CopyFuture::CopyFuture(CopyFuture&& Other) noexcept
    : mFuturePoints{ std::move(Other.mFuturePoints) } {
}

CopyFuture& CopyFuture::operator=(CopyFuture&& Other) noexcept {
    if (this != &Other) {
        mFuturePoints = std::move(Other.mFuturePoints);
    }

    return *this;
}

CopyFuture CopyFuture::Merge(std::span<const CopyFuture> Futures) {
    CopyFuture MergedFuture{};

    for (const CopyFuture& Future : Futures) {
        for (const FuturePoint& Point : Future.mFuturePoints) {
            if (MergedFuture.Contains(Point.Queue, Point.Ticket) == false) {
                MergedFuture.mFuturePoints.push_back(Point);
            }
        }
    }

    return MergedFuture;
}

bool CopyFuture::IsValid() const {
    return mFuturePoints.empty() == false;
}

bool CopyFuture::IsComplete() const {
    if (IsValid() == false) {
        return true;
    }

    for (const FuturePoint& Point : mFuturePoints) {
        if (Point.Queue == nullptr) {
            continue;
        }

        if (Point.Queue->IsFutureComplete(Point.Ticket) == false) {
            return false;
        }
    }

    return true;
}

bool CopyFuture::IsInFlight() const {
    return IsComplete() == false;
}

void CopyFuture::Wait() const {
    for (const FuturePoint& Point : mFuturePoints) {
        if (Point.Queue == nullptr) {
            continue;
        }

        Point.Queue->WaitFuture(Point.Ticket);
    }
}

void CopyFuture::QueueWait(ID3D12CommandQueue* WaitingQueue) const {
    if (WaitingQueue == nullptr) {
        return;
    }

    for (const FuturePoint& Point : mFuturePoints) {
        if (Point.Queue == nullptr) {
            continue;
        }

        Point.Queue->QueueWaitFuture(WaitingQueue, Point.Ticket);
    }
}

CopyFuture::CopyFuture(const ICopyQueue* Queue, std::uint64_t Ticket)
    : mFuturePoints{} {
    if (Queue != nullptr and Ticket > 0) {
        FuturePoint Point{};
        Point.Queue = Queue;
        Point.Ticket = Ticket;
        mFuturePoints.push_back(Point);
    }
}

bool CopyFuture::Contains(const ICopyQueue* Queue, std::uint64_t Ticket) const {
    for (const FuturePoint& Point : mFuturePoints) {
        if (Point.Queue == Queue and Point.Ticket == Ticket) {
            return true;
        }
    }

    return false;
}
