#include "CopyQueue.h"
#include <array>
#include <cstring>
#include <utility>
#include "Utility/ErrorHandler.h"

using namespace Core::DX;

CopyQueue::CopyQueue(ID3D12Device* device)
    : mCopyCommandQueue{},
    mCopyCommandAllocators{},
    mCopyCommandList{},
    mCopyFence{},
    mUploadHeapResource{},
    mUploadHeapMappedData{},
    mUploadBufferSize{ DefaultUploadBufferSize },
    mFenceEvent{},
    mQueueMutex{},
    mFenceMutex{},
    mQueueCondition{},
    mPendingRequestBatches{},
    mDispatchRequested{ false },
    mWorkerThread{},
    mIsRunning{ true },
    mFenceValueCounter{ 0 },
    mAllocatorCursor{ 0 },
    mAllocatorFenceValues{} {
    bool InitializeResult{ Initialize(device) };
    ErrorHandler::report(InitializeResult == false, "CopyQueue", "Failed to initialize copy queue.", ErrorHandler::Level::Critical);
}

CopyQueue::~CopyQueue() {
    StopWorker();

    if (mUploadHeapResource != nullptr) {
        mUploadHeapResource->Unmap(0, nullptr);
        mUploadHeapMappedData = nullptr;
    }

    if (mFenceEvent != nullptr) {
        CloseHandle(mFenceEvent);
        mFenceEvent = nullptr;
    }
}

bool CopyQueue::Initialize(ID3D12Device* device) {
    if (device == nullptr) {
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC QueueDescription{};
    QueueDescription.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    QueueDescription.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    QueueDescription.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    QueueDescription.NodeMask = 0;
    ErrorHandler::report(device->CreateCommandQueue(&QueueDescription, IID_PPV_ARGS(mCopyCommandQueue.GetAddressOf())), "CopyQueue", "Failed to create copy command queue.", ErrorHandler::Level::Critical);

    for (size_t AllocatorIndex{ 0 }; AllocatorIndex < CopyAllocatorCount; AllocatorIndex++) {
        ErrorHandler::report(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(mCopyCommandAllocators[AllocatorIndex].GetAddressOf())), "CopyQueue", "Failed to create copy command allocator.", ErrorHandler::Level::Critical);
    }

    ErrorHandler::report(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, mCopyCommandAllocators[0].Get(), nullptr, IID_PPV_ARGS(mCopyCommandList.GetAddressOf())), "CopyQueue", "Failed to create copy command list.", ErrorHandler::Level::Critical);
    ErrorHandler::report(mCopyCommandList->Close(), "CopyQueue", "Failed to close initial copy command list.", ErrorHandler::Level::Critical);
    ErrorHandler::report(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(mCopyFence.GetAddressOf())), "CopyQueue", "Failed to create copy queue fence.", ErrorHandler::Level::Critical);

    D3D12_HEAP_PROPERTIES UploadHeapProperties{};
    UploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    UploadHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    UploadHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    UploadHeapProperties.CreationNodeMask = 1;
    UploadHeapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC UploadBufferDescription{};
    UploadBufferDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    UploadBufferDescription.Alignment = 0;
    UploadBufferDescription.Width = mUploadBufferSize;
    UploadBufferDescription.Height = 1;
    UploadBufferDescription.DepthOrArraySize = 1;
    UploadBufferDescription.MipLevels = 1;
    UploadBufferDescription.Format = DXGI_FORMAT_UNKNOWN;
    UploadBufferDescription.SampleDesc.Count = 1;
    UploadBufferDescription.SampleDesc.Quality = 0;
    UploadBufferDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    UploadBufferDescription.Flags = D3D12_RESOURCE_FLAG_NONE;

    ErrorHandler::report(device->CreateCommittedResource(&UploadHeapProperties, D3D12_HEAP_FLAG_NONE, &UploadBufferDescription, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(mUploadHeapResource.GetAddressOf())), "CopyQueue", "Failed to create copy queue upload buffer.", ErrorHandler::Level::Critical);
    ErrorHandler::report(mUploadHeapResource->Map(0, nullptr, reinterpret_cast<void**>(&mUploadHeapMappedData)), "CopyQueue", "Failed to map copy queue upload buffer.", ErrorHandler::Level::Critical);

    mFenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    ErrorHandler::report(mFenceEvent == nullptr, "CopyQueue", "Failed to create copy queue fence event.", ErrorHandler::Level::Critical);

    mWorkerThread = std::thread(&CopyQueue::WorkerLoop, this);
    return true;
}

uint64_t CopyQueue::EnqueueCopy(const CopyQueueCopyRequest& copyRequest) {
    std::array<CopyQueueCopyRequest, 1> CopyRequests{ copyRequest };
    return EnqueueCopy(CopyRequests);
}

uint64_t CopyQueue::EnqueueCopy(std::span<const CopyQueueCopyRequest> copyRequests) {
    uint64_t NewFenceValue{ mFenceValueCounter.fetch_add(1) + 1 };
    size_t AllocatorIndex{ mAllocatorCursor.fetch_add(1) % CopyAllocatorCount };

    CopyRequestBatch RequestBatch{};
    RequestBatch.FenceValue = NewFenceValue;
    RequestBatch.AllocatorIndex = AllocatorIndex;
    RequestBatch.CopyRequests.assign(copyRequests.begin(), copyRequests.end());

    {
        std::lock_guard<std::mutex> QueueGuard{ mQueueMutex };
        mPendingRequestBatches.push(std::move(RequestBatch));
    }

    return NewFenceValue;
}

void CopyQueue::DispatchCopies() {
    {
        std::lock_guard<std::mutex> QueueGuard{ mQueueMutex };
        mDispatchRequested = true;
    }

    mQueueCondition.notify_one();
}

bool CopyQueue::IsFenceComplete(uint64_t fenceValue) const {
    std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
    return mCopyFence->GetCompletedValue() >= fenceValue;
}

void CopyQueue::WaitForFence(uint64_t fenceValue) const {
    if (IsFenceComplete(fenceValue)) {
        return;
    }

    std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
    ErrorHandler::report(mCopyFence->SetEventOnCompletion(fenceValue, mFenceEvent), "CopyQueue", "Failed to set copy queue fence completion event.", ErrorHandler::Level::Critical);
    WaitForSingleObjectEx(mFenceEvent, INFINITE, FALSE);
}

void CopyQueue::Flush() {
    DispatchCopies();
    WaitForQueueIdle();
}

UINT64 CopyQueue::GetRequiredUploadBufferSize() {
    return DefaultUploadBufferSize;
}

void CopyQueue::WorkerLoop() {
    while (mIsRunning.load() == true) {
        CopyRequestBatch RequestBatch{};

        {
            std::unique_lock<std::mutex> QueueLock{ mQueueMutex };
            mQueueCondition.wait(QueueLock, [this] { return mIsRunning.load() == false or (mDispatchRequested == true and mPendingRequestBatches.empty() == false); });

            if (mIsRunning.load() == false and mPendingRequestBatches.empty() == true) {
                return;
            }

            RequestBatch = std::move(mPendingRequestBatches.front());
            mPendingRequestBatches.pop();

            if (mPendingRequestBatches.empty() == true) {
                mDispatchRequested = false;
            }
        }

        ExecuteRequestBatch(RequestBatch);
    }
}

void CopyQueue::ExecuteRequestBatch(const CopyRequestBatch& requestBatch) {
    uint64_t AllocatorFenceValue{ mAllocatorFenceValues[requestBatch.AllocatorIndex] };
    if (AllocatorFenceValue > 0) {
        WaitForFence(AllocatorFenceValue);
    }

    ComPtr<ID3D12CommandAllocator>& CommandAllocator{ mCopyCommandAllocators[requestBatch.AllocatorIndex] };
    ErrorHandler::report(CommandAllocator->Reset(), "CopyQueue", "Failed to reset copy command allocator.", ErrorHandler::Level::Critical);
    ErrorHandler::report(mCopyCommandList->Reset(CommandAllocator.Get(), nullptr), "CopyQueue", "Failed to reset copy command list.", ErrorHandler::Level::Critical);

    for (const CopyQueueCopyRequest& CopyRequest : requestBatch.CopyRequests) {
        UINT64 RemainingSize{ static_cast<UINT64>(CopyRequest.SourceData.size()) };
        UINT64 SourceOffset{};
        while (RemainingSize > 0) {
            UINT64 ChunkSize{ RemainingSize > mUploadBufferSize ? mUploadBufferSize : RemainingSize };
            std::memcpy(mUploadHeapMappedData, CopyRequest.SourceData.data() + SourceOffset, static_cast<size_t>(ChunkSize));
            mCopyCommandList->CopyBufferRegion(CopyRequest.DestinationDefaultResource.Get(), CopyRequest.DestinationOffset + SourceOffset, mUploadHeapResource.Get(), 0, ChunkSize);
            SourceOffset += ChunkSize;
            RemainingSize -= ChunkSize;
        }
    }

    ErrorHandler::report(mCopyCommandList->Close(), "CopyQueue", "Failed to close copy command list.", ErrorHandler::Level::Critical);

    ID3D12CommandList* CommandLists[]{ mCopyCommandList.Get() };
    mCopyCommandQueue->ExecuteCommandLists(1, CommandLists);

    {
        std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
        ErrorHandler::report(mCopyCommandQueue->Signal(mCopyFence.Get(), requestBatch.FenceValue), "CopyQueue", "Failed to signal copy queue fence.", ErrorHandler::Level::Critical);
    }

    mAllocatorFenceValues[requestBatch.AllocatorIndex] = requestBatch.FenceValue;
}

void CopyQueue::StopWorker() {
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
    uint64_t TargetFenceValue{ mFenceValueCounter.load() };
    if (TargetFenceValue == 0) {
        return;
    }

    WaitForFence(TargetFenceValue);
}
