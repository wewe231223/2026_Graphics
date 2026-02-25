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
    mUploadHeapSlots{},
    mUploadBufferSize{ DefaultUploadBufferSize },
    mCurrentUploadHeapSlotIndex{},
    mFenceEvent{},
    mQueueMutex{},
    mFenceMutex{},
    mQueueCondition{},
    mPendingRequestBatches{},
    mDispatchRequested{ false },
    mWorkerThread{},
    mIsRunning{ true },
    mRequestedFenceValueCounter{ 0 },
    mSubmitFenceValueCounter{ 0 },
    mAllocatorFenceValues{},
    mRequestedFenceToCompletedSubmitFence{} {
    bool InitializeResult{ Initialize(device) };
    ErrorHandler::report(InitializeResult == false, "CopyQueue", "Failed to initialize copy queue.", ErrorHandler::Level::Critical);
}

CopyQueue::~CopyQueue() {
    StopWorker();

    for (UploadHeapSlot& UploadHeapSlot : mUploadHeapSlots) {
        if (UploadHeapSlot.UploadHeapResource != nullptr) {
            UploadHeapSlot.UploadHeapResource->Unmap(0, nullptr);
            UploadHeapSlot.UploadHeapMappedData = nullptr;
        }
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

    for (UploadHeapSlot& UploadHeapSlot : mUploadHeapSlots) {
        UploadHeapSlot.UploadHeapMappedData = nullptr;
        UploadHeapSlot.WriteOffset = 0;
        UploadHeapSlot.SlotFenceValue = 0;
        ErrorHandler::report(device->CreateCommittedResource(&UploadHeapProperties, D3D12_HEAP_FLAG_NONE, &UploadBufferDescription, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(UploadHeapSlot.UploadHeapResource.GetAddressOf())), "CopyQueue", "Failed to create copy queue upload buffer.", ErrorHandler::Level::Critical);
        ErrorHandler::report(UploadHeapSlot.UploadHeapResource->Map(0, nullptr, reinterpret_cast<void**>(&UploadHeapSlot.UploadHeapMappedData)), "CopyQueue", "Failed to map copy queue upload buffer.", ErrorHandler::Level::Critical);
    }

    mFenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    ErrorHandler::report(mFenceEvent == nullptr, "CopyQueue", "Failed to create copy queue fence event.", ErrorHandler::Level::Critical);

    mWorkerThread = std::thread(&CopyQueue::WorkerLoop, this);
    return true;
}

uint64_t CopyQueue::EnqueueCopy(const Interface::CopyQueueCopyRequest& copyRequest) {
    std::array<Interface::CopyQueueCopyRequest, 1> CopyRequests{ copyRequest };
    return EnqueueCopy(CopyRequests);
}

uint64_t CopyQueue::EnqueueCopy(std::span<const Interface::CopyQueueCopyRequest> copyRequests) {
    uint64_t RequestedFenceValue{ mRequestedFenceValueCounter.fetch_add(1) + 1 };

    CopyRequestBatch RequestBatch{};
    RequestBatch.RequestedFenceValue = RequestedFenceValue;
    RequestBatch.CopyRequests.assign(copyRequests.begin(), copyRequests.end());

    {
        std::lock_guard<std::mutex> QueueGuard{ mQueueMutex };
        mPendingRequestBatches.push(std::move(RequestBatch));
    }

    {
        std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
        mRequestedFenceToCompletedSubmitFence.emplace(RequestedFenceValue, mSubmitFenceValueCounter.load());
    }

    return RequestedFenceValue;
}

void CopyQueue::DispatchCopies() {
    {
        std::lock_guard<std::mutex> QueueGuard{ mQueueMutex };
        mDispatchRequested = true;
    }

    mQueueCondition.notify_one();
}

bool CopyQueue::IsFenceComplete(uint64_t fenceValue) const {
    uint64_t SubmitFenceValue{ ResolveRequestedFenceValue(fenceValue) };
    return IsSubmitFenceComplete(SubmitFenceValue);
}

void CopyQueue::WaitForFence(uint64_t fenceValue) const {
    uint64_t SubmitFenceValue{ ResolveRequestedFenceValue(fenceValue) };
    WaitForSubmitFence(SubmitFenceValue);
}

void CopyQueue::Flush() {
    DispatchCopies();
    WaitForQueueIdle();
}

uint64_t CopyQueue::GetRequiredUploadBufferSize() const {
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
    if (requestBatch.CopyRequests.empty() == true) {
        std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
        mRequestedFenceToCompletedSubmitFence[requestBatch.RequestedFenceValue] = mSubmitFenceValueCounter.load();
        return;
    }

    size_t AllocatorIndex{ 0 };
    std::vector<bool> RequestTouchedMask(requestBatch.CopyRequests.size(), false);
    std::vector<uint64_t> RequestCompletedSubmitFenceValues(requestBatch.CopyRequests.size(), 0);

    ErrorHandler::report(mCopyCommandAllocators[AllocatorIndex]->Reset(), "CopyQueue", "Failed to reset copy command allocator.", ErrorHandler::Level::Critical);
    ErrorHandler::report(mCopyCommandList->Reset(mCopyCommandAllocators[AllocatorIndex].Get(), nullptr), "CopyQueue", "Failed to reset copy command list.", ErrorHandler::Level::Critical);

    for (size_t RequestIndex{ 0 }; RequestIndex < requestBatch.CopyRequests.size(); RequestIndex++) {
        const Interface::CopyQueueCopyRequest& CopyRequest{ requestBatch.CopyRequests[RequestIndex] };
        UINT64 RemainingSize{ static_cast<UINT64>(CopyRequest.SourceData.size()) };
        UINT64 SourceOffset{};

        while (RemainingSize > 0) {
            UploadHeapSlot& UploadHeapSlot{ mUploadHeapSlots[mCurrentUploadHeapSlotIndex] };
            UINT64 RemainingSlotCapacity{ mUploadBufferSize - UploadHeapSlot.WriteOffset };

            if (RemainingSlotCapacity == 0) {
                bool Switched{ TrySwitchUploadHeapSlot(mCurrentUploadHeapSlotIndex, AllocatorIndex, RequestTouchedMask, RequestCompletedSubmitFenceValues) };
                ErrorHandler::report(Switched == false, "CopyQueue", "Failed to switch upload heap slot.", ErrorHandler::Level::Critical);
                continue;
            }

            UINT64 ChunkSize{ RemainingSize > RemainingSlotCapacity ? RemainingSlotCapacity : RemainingSize };
            std::memcpy(UploadHeapSlot.UploadHeapMappedData + UploadHeapSlot.WriteOffset, CopyRequest.SourceData.data() + SourceOffset, static_cast<size_t>(ChunkSize));
            mCopyCommandList->CopyBufferRegion(CopyRequest.DestinationDefaultResource.Get(), CopyRequest.DestinationOffset + SourceOffset, UploadHeapSlot.UploadHeapResource.Get(), UploadHeapSlot.WriteOffset, ChunkSize);
            UploadHeapSlot.WriteOffset += ChunkSize;
            SourceOffset += ChunkSize;
            RemainingSize -= ChunkSize;
            RequestTouchedMask[RequestIndex] = true;
        }
    }

    uint64_t FinalSubmitFenceValue{ SubmitCurrentCommandList(AllocatorIndex, RequestTouchedMask, RequestCompletedSubmitFenceValues) };
    ErrorHandler::report(FinalSubmitFenceValue == 0, "CopyQueue", "Failed to submit copy command list.", ErrorHandler::Level::Critical);

    uint64_t RequestedFenceCompletedSubmitFenceValue{ 0 };
    for (uint64_t RequestCompletedSubmitFenceValue : RequestCompletedSubmitFenceValues) {
        if (RequestedFenceCompletedSubmitFenceValue < RequestCompletedSubmitFenceValue) {
            RequestedFenceCompletedSubmitFenceValue = RequestCompletedSubmitFenceValue;
        }
    }

    {
        std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
        mRequestedFenceToCompletedSubmitFence[requestBatch.RequestedFenceValue] = RequestedFenceCompletedSubmitFenceValue;
    }
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

void CopyQueue::WaitForQueueIdle() {
    uint64_t RequestedFenceValue{ EnqueueCopy(std::span<const Interface::CopyQueueCopyRequest>{}) };
    DispatchCopies();
    WaitForFence(RequestedFenceValue);
}


bool CopyQueue::IsSubmitFenceComplete(uint64_t fenceValue) const {
    return mCopyFence->GetCompletedValue() >= fenceValue;
}

void CopyQueue::WaitForSubmitFence(uint64_t fenceValue) const {
    if (IsSubmitFenceComplete(fenceValue) == true) {
        return;
    }

    std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
    ErrorHandler::report(mCopyFence->SetEventOnCompletion(fenceValue, mFenceEvent), "CopyQueue", "Failed to set copy queue fence completion event.", ErrorHandler::Level::Critical);
    WaitForSingleObjectEx(mFenceEvent, INFINITE, FALSE);
}

uint64_t CopyQueue::ResolveRequestedFenceValue(uint64_t fenceValue) const {
    std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
    std::unordered_map<uint64_t, uint64_t>::const_iterator FoundFence{ mRequestedFenceToCompletedSubmitFence.find(fenceValue) };
    if (FoundFence == mRequestedFenceToCompletedSubmitFence.end()) {
        return fenceValue;
    }

    return FoundFence->second;
}

uint64_t CopyQueue::SubmitCurrentCommandList(size_t allocatorIndex, std::vector<bool>& requestTouchedMask, std::vector<uint64_t>& requestCompletedSubmitFenceValues) {
    ErrorHandler::report(mCopyCommandList->Close(), "CopyQueue", "Failed to close copy command list.", ErrorHandler::Level::Critical);

    ID3D12CommandList* CommandLists[]{ mCopyCommandList.Get() };
    mCopyCommandQueue->ExecuteCommandLists(1, CommandLists);

    uint64_t SubmitFenceValue{ mSubmitFenceValueCounter.fetch_add(1) + 1 };
    {
        std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
        ErrorHandler::report(mCopyCommandQueue->Signal(mCopyFence.Get(), SubmitFenceValue), "CopyQueue", "Failed to signal copy queue fence.", ErrorHandler::Level::Critical);
    }

    mAllocatorFenceValues[allocatorIndex] = SubmitFenceValue;
    mUploadHeapSlots[mCurrentUploadHeapSlotIndex].SlotFenceValue = SubmitFenceValue;

    for (size_t RequestIndex{ 0 }; RequestIndex < requestTouchedMask.size(); RequestIndex++) {
        if (requestTouchedMask[RequestIndex] == true and requestCompletedSubmitFenceValues[RequestIndex] < SubmitFenceValue) {
            requestCompletedSubmitFenceValues[RequestIndex] = SubmitFenceValue;
            requestTouchedMask[RequestIndex] = false;
        }
    }

    return SubmitFenceValue;
}

bool CopyQueue::TrySwitchUploadHeapSlot(size_t& slotIndex, size_t& allocatorIndex, std::vector<bool>& requestTouchedMask, std::vector<uint64_t>& requestCompletedSubmitFenceValues) {
    uint64_t SubmittedFenceValue{ SubmitCurrentCommandList(allocatorIndex, requestTouchedMask, requestCompletedSubmitFenceValues) };
    if (SubmittedFenceValue == 0) {
        return false;
    }

    slotIndex = (slotIndex + 1) % UploadHeapSlotCount;
    UploadHeapSlot& NextUploadHeapSlot{ mUploadHeapSlots[slotIndex] };
    if (NextUploadHeapSlot.SlotFenceValue > 0) {
        WaitForSubmitFence(NextUploadHeapSlot.SlotFenceValue);
    }

    NextUploadHeapSlot.WriteOffset = 0;

    allocatorIndex = (allocatorIndex + 1) % CopyAllocatorCount;
    uint64_t AllocatorFenceValue{ mAllocatorFenceValues[allocatorIndex] };
    if (AllocatorFenceValue > 0) {
        WaitForSubmitFence(AllocatorFenceValue);
    }

    ErrorHandler::report(mCopyCommandAllocators[allocatorIndex]->Reset(), "CopyQueue", "Failed to reset copy command allocator.", ErrorHandler::Level::Critical);
    ErrorHandler::report(mCopyCommandList->Reset(mCopyCommandAllocators[allocatorIndex].Get(), nullptr), "CopyQueue", "Failed to reset copy command list.", ErrorHandler::Level::Critical);
    return true;
}
