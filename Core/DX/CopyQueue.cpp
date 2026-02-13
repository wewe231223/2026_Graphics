#include "CopyQueue.h"
#include <array>
#include <cstring>
#include <utility>
#include "Utility/ErrorHandler.h"

using namespace Core::DX;

CopyQueue::CopyQueue(ID3D12Device* device)
    : mIsRunning{ true },
    mFenceValueCounter{ 0 },
    mAllocatorCursor{ 0 },
    mAllocatorFenceValues{},
    mDispatchRequested{ false },
    mUploadHeapMappedData{} {

    D3D12_COMMAND_QUEUE_DESC QueueDescription{};
    QueueDescription.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    QueueDescription.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    QueueDescription.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    QueueDescription.NodeMask = 0;

    ErrorHandler::report(device->CreateCommandQueue(&QueueDescription, IID_PPV_ARGS(mCopyCommandQueue.GetAddressOf())), "CopyQueue", "Failed to create copy command queue.", ErrorHandler::Level::Critical);

    for (size_t allocatorIndex{ 0 }; allocatorIndex < CopyAllocatorCount; allocatorIndex++) {
        ErrorHandler::report(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(mCopyCommandAllocators[allocatorIndex].GetAddressOf())), "CopyQueue", "Failed to create copy command allocator.", ErrorHandler::Level::Critical);
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

    D3D12_RESOURCE_DESC UploadResourceDescription{};
    UploadResourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    UploadResourceDescription.Alignment = 0;
    UploadResourceDescription.Width = UploadHeapSize;
    UploadResourceDescription.Height = 1;
    UploadResourceDescription.DepthOrArraySize = 1;
    UploadResourceDescription.MipLevels = 1;
    UploadResourceDescription.Format = DXGI_FORMAT_UNKNOWN;
    UploadResourceDescription.SampleDesc.Count = 1;
    UploadResourceDescription.SampleDesc.Quality = 0;
    UploadResourceDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    UploadResourceDescription.Flags = D3D12_RESOURCE_FLAG_NONE;

    ErrorHandler::report(device->CreateCommittedResource(&UploadHeapProperties, D3D12_HEAP_FLAG_NONE, &UploadResourceDescription, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(mUploadHeapResource.GetAddressOf())), "CopyQueue", "Failed to create copy queue upload heap resource.", ErrorHandler::Level::Critical);
    ErrorHandler::report(mUploadHeapResource->Map(0, nullptr, reinterpret_cast<void**>(&mUploadHeapMappedData)), "CopyQueue", "Failed to map copy queue upload heap resource.", ErrorHandler::Level::Critical);


    mFenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    ErrorHandler::report(mFenceEvent == nullptr, "CopyQueue", "Failed to create copy queue fence event.", ErrorHandler::Level::Critical);

    mWorkerThread = std::thread(&CopyQueue::WorkerLoop, this);
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

uint64_t CopyQueue::EnqueueCopy(const ComPtr<ID3D12Resource>& destinationDefaultResource, UINT64 destinationOffset, std::span<const std::byte> sourceData) {

    CopyQueueCopyRequest CopyRequest{};
    CopyRequest.DestinationDefaultResource = destinationDefaultResource;
    CopyRequest.DestinationOffset = destinationOffset;
    CopyRequest.SourceData.assign(sourceData.begin(), sourceData.end());
    std::array<CopyQueueCopyRequest, 1> copyRequests{ std::move(CopyRequest) };

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

    for (const CopyQueueCopyRequest& CopyRequest : requestBatch.CopyRequests) {
        UINT64 RemainingSize{ static_cast<UINT64>(CopyRequest.SourceData.size()) };
        UINT64 SourceOffset{};
        while (RemainingSize > 0) {
            UINT64 ChunkSize{ RemainingSize > UploadHeapSize ? UploadHeapSize : RemainingSize };
            std::memcpy(mUploadHeapMappedData, CopyRequest.SourceData.data() + SourceOffset, static_cast<size_t>(ChunkSize));
            mCopyCommandList->CopyBufferRegion(CopyRequest.DestinationDefaultResource.Get(), CopyRequest.DestinationOffset + SourceOffset, mUploadHeapResource.Get(), 0, ChunkSize);
            SourceOffset += ChunkSize;
            RemainingSize -= ChunkSize;
        }
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