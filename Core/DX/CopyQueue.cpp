#include "CopyQueue.h"
#include <array>
#include "Utility/ErrorHandler.h"

using namespace Core::DX;

CopyQueue::CopyQueue(ID3D12Device* device)
    : mIsRunning{ true },
    mFenceValueCounter{ 0 } {
    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    queueDescription.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDescription.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDescription.NodeMask = 0;

    ErrorHandler::report(device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(mCopyCommandQueue.GetAddressOf())), "CopyQueue", "Failed to create copy command queue.", ErrorHandler::Level::Critical);
    ErrorHandler::report(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(mCopyCommandAllocator.GetAddressOf())), "CopyQueue", "Failed to create copy command allocator.", ErrorHandler::Level::Critical);
    ErrorHandler::report(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, mCopyCommandAllocator.Get(), nullptr, IID_PPV_ARGS(mCopyCommandList.GetAddressOf())), "CopyQueue", "Failed to create copy command list.", ErrorHandler::Level::Critical);
    ErrorHandler::report(mCopyCommandList->Close(), "CopyQueue", "Failed to close initial copy command list.", ErrorHandler::Level::Critical);
    ErrorHandler::report(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(mCopyFence.GetAddressOf())), "CopyQueue", "Failed to create copy queue fence.", ErrorHandler::Level::Critical);

    mFenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    ErrorHandler::report(mFenceEvent == nullptr, "CopyQueue", "Failed to create copy queue fence event.", ErrorHandler::Level::Critical);

    mWorkerThread = std::thread(&CopyQueue::WorkerLoop, this);
}

CopyQueue::~CopyQueue() {
    StopWorker();
    if (mFenceEvent != nullptr) {
        CloseHandle(mFenceEvent);
        mFenceEvent = nullptr;
    }
}

uint64_t CopyQueue::EnqueueCopy(ID3D12Resource* destinationDefaultResource, UINT64 destinationOffset, ID3D12Resource* sourceUploadResource, UINT64 sourceOffset, UINT64 copySize) {
    std::array<CopyQueueCopyRequest, 1> copyRequests{ CopyQueueCopyRequest{ destinationDefaultResource, destinationOffset, sourceUploadResource, sourceOffset, copySize } };
    return EnqueueCopy(copyRequests);
}

uint64_t CopyQueue::EnqueueCopy(std::span<const CopyQueueCopyRequest> copyRequests) {
    uint64_t newFenceValue{ mFenceValueCounter.fetch_add(1) + 1 };

    CopyRequestBatch requestBatch{};
    requestBatch.FenceValue = newFenceValue;
    requestBatch.CopyRequests.assign(copyRequests.begin(), copyRequests.end());

    {
        std::lock_guard<std::mutex> queueGuard{ mQueueMutex };
        mPendingRequestBatches.push(std::move(requestBatch));
    }

    mQueueCondition.notify_one();
    return newFenceValue;
}

bool CopyQueue::IsFenceComplete(uint64_t fenceValue) const {
    std::lock_guard<std::mutex> fenceGuard{ mFenceMutex };
    return mCopyFence->GetCompletedValue() >= fenceValue;
}

void CopyQueue::WaitForFence(uint64_t fenceValue) const {
    if (IsFenceComplete(fenceValue)) {
        return;
    }

    std::lock_guard<std::mutex> fenceGuard{ mFenceMutex };
    ErrorHandler::report(mCopyFence->SetEventOnCompletion(fenceValue, mFenceEvent), "CopyQueue", "Failed to set copy queue fence completion event.", ErrorHandler::Level::Critical);
    WaitForSingleObjectEx(mFenceEvent, INFINITE, FALSE);
}

void CopyQueue::Flush() {
    WaitForQueueIdle();
}

void CopyQueue::WorkerLoop() {
    while (mIsRunning.load() == true) {
        CopyRequestBatch requestBatch{};

        {
            std::unique_lock<std::mutex> queueLock{ mQueueMutex };
            mQueueCondition.wait(queueLock, [this] { return mPendingRequestBatches.empty() == false || mIsRunning.load() == false; });
            if (mIsRunning.load() == false && mPendingRequestBatches.empty() == true) {
                return;
            }
            requestBatch = std::move(mPendingRequestBatches.front());
            mPendingRequestBatches.pop();
        }

        ExecuteRequestBatch(requestBatch);
    }
}

void CopyQueue::ExecuteRequestBatch(const CopyRequestBatch& requestBatch) {
    ErrorHandler::report(mCopyCommandAllocator->Reset(), "CopyQueue", "Failed to reset copy command allocator.", ErrorHandler::Level::Critical);
    ErrorHandler::report(mCopyCommandList->Reset(mCopyCommandAllocator.Get(), nullptr), "CopyQueue", "Failed to reset copy command list.", ErrorHandler::Level::Critical);

    for (const CopyQueueCopyRequest& copyRequest : requestBatch.CopyRequests) {
        mCopyCommandList->CopyBufferRegion(copyRequest.DestinationDefaultResource, copyRequest.DestinationOffset, copyRequest.SourceUploadResource, copyRequest.SourceOffset, copyRequest.CopySize);
    }

    ErrorHandler::report(mCopyCommandList->Close(), "CopyQueue", "Failed to close copy command list.", ErrorHandler::Level::Critical);

    ID3D12CommandList* commandLists[]{ mCopyCommandList.Get() };
    mCopyCommandQueue->ExecuteCommandLists(1, commandLists);

    {
        std::lock_guard<std::mutex> fenceGuard{ mFenceMutex };
        ErrorHandler::report(mCopyCommandQueue->Signal(mCopyFence.Get(), requestBatch.FenceValue), "CopyQueue", "Failed to signal copy queue fence.", ErrorHandler::Level::Critical);
    }
}

void CopyQueue::StopWorker() {
    bool expectedRunning{ true };
    if (mIsRunning.compare_exchange_strong(expectedRunning, false) == true) {
        mQueueCondition.notify_all();
        if (mWorkerThread.joinable() == true) {
            mWorkerThread.join();
        }
        WaitForQueueIdle();
    }
}

void CopyQueue::WaitForQueueIdle() const {
    uint64_t targetFenceValue{ mFenceValueCounter.load() };
    if (targetFenceValue == 0) {
        return;
    }
    WaitForFence(targetFenceValue);
}