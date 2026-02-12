#include "CopyQueue.h"
#include <array>
#include "Utility/ErrorHandler.h"

using namespace Core::DX;

CopyQueue::CopyQueue(ID3D12Device* device)
    : mIsRunning{ true },
    mFenceValueCounter{ 0 },
    mAllocatorCursor{ 0 },
    mAllocatorFenceValues{},
    mDispatchRequested{ false } {
    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    queueDescription.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDescription.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDescription.NodeMask = 0;

    ErrorHandler::report(device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(mCopyCommandQueue.GetAddressOf())), "CopyQueue", "Failed to create copy command queue.", ErrorHandler::Level::Critical);

    for (size_t allocatorIndex{ 0 }; allocatorIndex < CopyAllocatorCount; allocatorIndex++) {
        ErrorHandler::report(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(mCopyCommandAllocators[allocatorIndex].GetAddressOf())), "CopyQueue", "Failed to create copy command allocator.", ErrorHandler::Level::Critical);
    }

    ErrorHandler::report(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, mCopyCommandAllocators[0].Get(), nullptr, IID_PPV_ARGS(mCopyCommandList.GetAddressOf())), "CopyQueue", "Failed to create copy command list.", ErrorHandler::Level::Critical);
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

uint64_t CopyQueue::EnqueueCopy(const ComPtr<ID3D12Resource>& destinationDefaultResource, UINT64 destinationOffset, const ComPtr<ID3D12Resource>& sourceUploadResource, UINT64 sourceOffset, UINT64 copySize) {
    std::array<CopyQueueCopyRequest, 1> copyRequests{ CopyQueueCopyRequest{ destinationDefaultResource, destinationOffset, sourceUploadResource, sourceOffset, copySize } };
    return EnqueueCopy(copyRequests);
}

uint64_t CopyQueue::EnqueueCopy(std::span<const CopyQueueCopyRequest> copyRequests) {
    uint64_t newFenceValue{ mFenceValueCounter.fetch_add(1) + 1 };
    size_t allocatorIndex{ mAllocatorCursor.fetch_add(1) % CopyAllocatorCount };

    CopyRequestBatch requestBatch{};
    requestBatch.FenceValue = newFenceValue;
    requestBatch.AllocatorIndex = allocatorIndex;
    requestBatch.CopyRequests.assign(copyRequests.begin(), copyRequests.end());

    {
        std::lock_guard<std::mutex> queueGuard{ mQueueMutex };
        mPendingRequestBatches.push(std::move(requestBatch));
    }

    return newFenceValue;
}

void CopyQueue::DispatchCopies() {
    {
        std::lock_guard<std::mutex> queueGuard{ mQueueMutex };
        mDispatchRequested = true;
    }

    mQueueCondition.notify_one();
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
    DispatchCopies(); 
    WaitForQueueIdle();
}

void CopyQueue::WorkerLoop() {
    while (mIsRunning.load() == true) {
        CopyRequestBatch requestBatch{};

        {
            std::unique_lock<std::mutex> queueLock{ mQueueMutex };
            
            mQueueCondition.wait(queueLock, [this] { 
                return mIsRunning.load() == false or (mDispatchRequested == true and mPendingRequestBatches.empty() == false); 
                }
            );

            if (mIsRunning.load() == false and mPendingRequestBatches.empty() == true) {
                return;
            }
            
            requestBatch = std::move(mPendingRequestBatches.front());
            mPendingRequestBatches.pop();

            if (mPendingRequestBatches.empty() == true) {
                mDispatchRequested = false;
            }
        }

        ExecuteRequestBatch(requestBatch);
    }
}

void CopyQueue::ExecuteRequestBatch(const CopyRequestBatch& requestBatch) {
    uint64_t allocatorFenceValue{ mAllocatorFenceValues[requestBatch.AllocatorIndex] };
    if (allocatorFenceValue > 0) {
        WaitForFence(allocatorFenceValue);
    }

    ComPtr<ID3D12CommandAllocator>& commandAllocator = mCopyCommandAllocators[requestBatch.AllocatorIndex];
    ErrorHandler::report(commandAllocator->Reset(), "CopyQueue", "Failed to reset copy command allocator.", ErrorHandler::Level::Critical);
    ErrorHandler::report(mCopyCommandList->Reset(commandAllocator.Get(), nullptr), "CopyQueue", "Failed to reset copy command list.", ErrorHandler::Level::Critical);

    for (const CopyQueueCopyRequest& copyRequest : requestBatch.CopyRequests) {
        mCopyCommandList->CopyBufferRegion(copyRequest.DestinationDefaultResource.Get(), copyRequest.DestinationOffset, copyRequest.SourceUploadResource.Get(), copyRequest.SourceOffset, copyRequest.CopySize);
    }

    ErrorHandler::report(mCopyCommandList->Close(), "CopyQueue", "Failed to close copy command list.", ErrorHandler::Level::Critical);

    ID3D12CommandList* commandLists[]{ mCopyCommandList.Get() };
    mCopyCommandQueue->ExecuteCommandLists(1, commandLists);

    {
        std::lock_guard<std::mutex> fenceGuard{ mFenceMutex };
        ErrorHandler::report(mCopyCommandQueue->Signal(mCopyFence.Get(), requestBatch.FenceValue), "CopyQueue", "Failed to signal copy queue fence.", ErrorHandler::Level::Critical);
    }

    mAllocatorFenceValues[requestBatch.AllocatorIndex] = requestBatch.FenceValue;
}

void CopyQueue::StopWorker() {
    bool expectedRunning{ true };
    if (mIsRunning.load() == true) {
        Flush(); 
		mIsRunning.store(false);
        mQueueCondition.notify_all();
        if (mWorkerThread.joinable() == true) {
            mWorkerThread.join();
        }
    }
}

void CopyQueue::WaitForQueueIdle() const {
    uint64_t targetFenceValue{ mFenceValueCounter.load() };
    if (targetFenceValue == 0) {
        return;
    }
    WaitForFence(targetFenceValue);
}