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
    mBufferUploadHeapCollection{},
    mTextureUploadHeapCollection{},
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
    bool initializeResult{ Initialize(device) };
    ErrorHandler::report(initializeResult == false, "CopyQueue", "Failed to initialize copy queue.", ErrorHandler::Level::Critical);
}

CopyQueue::~CopyQueue() {
    StopWorker();
    ResetUploadHeapCollection(mBufferUploadHeapCollection);
    ResetUploadHeapCollection(mTextureUploadHeapCollection);

    if (mFenceEvent != nullptr) {
        CloseHandle(mFenceEvent);
        mFenceEvent = nullptr;
    }
}

bool CopyQueue::Initialize(ID3D12Device* device) {
    if (device == nullptr) {
        return false;
    }

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

    bool bufferInitializeResult{ InitializeUploadHeapCollection(device, mBufferUploadHeapCollection, DefaultUploadBufferSize) };
    bool textureInitializeResult{ InitializeUploadHeapCollection(device, mTextureUploadHeapCollection, DefaultTextureUploadBufferSize) };
    ErrorHandler::report(bufferInitializeResult == false || textureInitializeResult == false, "CopyQueue", "Failed to initialize copy queue upload heap collection.", ErrorHandler::Level::Critical);

    mFenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    ErrorHandler::report(mFenceEvent == nullptr, "CopyQueue", "Failed to create copy queue fence event.", ErrorHandler::Level::Critical);

    mWorkerThread = std::thread(&CopyQueue::WorkerLoop, this);
    return true;
}

uint64_t CopyQueue::EnqueueCopy(const Interface::CopyRequest& copyRequest) {
    std::array<Interface::CopyRequest, 1> copyRequests{ copyRequest };
    return EnqueueCopy(copyRequests);
}

uint64_t CopyQueue::EnqueueCopy(std::span<const Interface::CopyRequest> copyRequests) {
    uint64_t requestedFenceValue{ mRequestedFenceValueCounter.fetch_add(1) + 1 };

    CopyRequestBatch requestBatch{};
    requestBatch.RequestedFenceValue = requestedFenceValue;
    requestBatch.CopyRequests.assign(copyRequests.begin(), copyRequests.end());

    {
        std::lock_guard<std::mutex> queueGuard{ mQueueMutex };
        mPendingRequestBatches.push(std::move(requestBatch));
    }

    {
        std::lock_guard<std::mutex> fenceGuard{ mFenceMutex };
        mRequestedFenceToCompletedSubmitFence.emplace(requestedFenceValue, PendingSubmitFenceValue);
    }

    return requestedFenceValue;
}

void CopyQueue::DispatchCopies() {
    {
        std::lock_guard<std::mutex> queueGuard{ mQueueMutex };
        mDispatchRequested = true;
    }

    mQueueCondition.notify_one();
}

bool CopyQueue::IsFenceComplete(uint64_t fenceValue) const {
    uint64_t submitFenceValue{ ResolveRequestedFenceValue(fenceValue) };
    return IsSubmitFenceComplete(submitFenceValue);
}

void CopyQueue::WaitForFence(uint64_t fenceValue) const {
    uint64_t submitFenceValue{ ResolveRequestedFenceValue(fenceValue) };
    WaitForSubmitFence(submitFenceValue);
}

void CopyQueue::Flush() {
    DispatchCopies();
    WaitForQueueIdle();
}

uint64_t CopyQueue::GetRequiredUploadBufferSize() const {
    return DefaultUploadBufferSize;
}

uint64_t CopyQueue::GetRequiredTextureUploadBufferSize() const {
    return DefaultTextureUploadBufferSize;
}

void CopyQueue::WorkerLoop() {
    while (mIsRunning.load() == true) {
        CopyRequestBatch requestBatch{};

        {
            std::unique_lock<std::mutex> queueLock{ mQueueMutex };
            mQueueCondition.wait(queueLock, [this] { return mIsRunning.load() == false || (mDispatchRequested == true && mPendingRequestBatches.empty() == false); });
            if (mIsRunning.load() == false && mPendingRequestBatches.empty() == true) {
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
    if (requestBatch.CopyRequests.empty() == true) {
        {
            std::lock_guard<std::mutex> fenceGuard{ mFenceMutex };
            mRequestedFenceToCompletedSubmitFence[requestBatch.RequestedFenceValue] = mSubmitFenceValueCounter.load();
        }

        mFenceCondition.notify_all();
        return;
    }

    size_t allocatorIndex{ 0 };
    std::vector<bool> requestTouchedMask(requestBatch.CopyRequests.size(), false);
    std::vector<uint64_t> requestCompletedSubmitFenceValues(requestBatch.CopyRequests.size(), 0);

    PrepareAllocatorForRecording(allocatorIndex);

    for (size_t requestIndex{ 0 }; requestIndex < requestBatch.CopyRequests.size(); requestIndex++) {
        const Interface::CopyRequest& copyRequest{ requestBatch.CopyRequests[requestIndex] };
        bool processResult{ false };
        if (copyRequest.CopyType == Interface::CopyQueueCopyType::Texture) {
            processResult = ProcessTextureCopyRequest(copyRequest, requestIndex, allocatorIndex, requestTouchedMask, requestCompletedSubmitFenceValues);
        }
        else {
            processResult = ProcessBufferCopyRequest(copyRequest, requestIndex, allocatorIndex, requestTouchedMask, requestCompletedSubmitFenceValues);
        }

        ErrorHandler::report(processResult == false, "CopyQueue", "Failed to process copy queue request.", ErrorHandler::Level::Critical);
    }

    uint64_t finalSubmitFenceValue{ SubmitCurrentCommandList(allocatorIndex, requestTouchedMask, requestCompletedSubmitFenceValues) };
    ErrorHandler::report(finalSubmitFenceValue == 0, "CopyQueue", "Failed to submit copy command list.", ErrorHandler::Level::Critical);

    uint64_t requestedFenceCompletedSubmitFenceValue{ 0 };
    for (uint64_t requestCompletedSubmitFenceValue : requestCompletedSubmitFenceValues) {
        if (requestedFenceCompletedSubmitFenceValue < requestCompletedSubmitFenceValue) {
            requestedFenceCompletedSubmitFenceValue = requestCompletedSubmitFenceValue;
        }
    }

    {
        std::lock_guard<std::mutex> fenceGuard{ mFenceMutex };
        mRequestedFenceToCompletedSubmitFence[requestBatch.RequestedFenceValue] = requestedFenceCompletedSubmitFenceValue;
    }

    mFenceCondition.notify_all();
}

bool CopyQueue::ProcessBufferCopyRequest(const Interface::CopyRequest& copyRequest, size_t requestIndex, size_t& allocatorIndex, std::vector<bool>& requestTouchedMask, std::vector<uint64_t>& requestCompletedSubmitFenceValues) {
    if (copyRequest.DestinationDefaultResource == nullptr) {
        return false;
    }

    return WriteUploadBytes(mBufferUploadHeapCollection, copyRequest.DestinationDefaultResource.Get(), copyRequest.DestinationOffset, copyRequest.SourceData.data(), static_cast<uint64_t>(copyRequest.SourceData.size()), requestIndex, allocatorIndex, requestTouchedMask, requestCompletedSubmitFenceValues);
}

bool CopyQueue::ProcessTextureCopyRequest(const Interface::CopyRequest& copyRequest, size_t requestIndex, size_t& allocatorIndex, std::vector<bool>& requestTouchedMask, std::vector<uint64_t>& requestCompletedSubmitFenceValues) {
    if (copyRequest.DestinationDefaultResource == nullptr || copyRequest.TextureSubresources.empty() == true || copyRequest.SourceData.empty() == true) {
        return false;
    }

    for (const Interface::CopyQueueTextureCopySubresource& subresource : copyRequest.TextureSubresources) {
        uint64_t subresourceOffset{ subresource.Footprint.Offset };
        uint64_t subresourceBytes{ static_cast<uint64_t>(subresource.Footprint.Footprint.RowPitch) * subresource.Footprint.Footprint.Height * subresource.Footprint.Footprint.Depth };
        if (subresourceOffset + subresourceBytes > copyRequest.SourceData.size() || subresourceBytes > mTextureUploadHeapCollection.BufferSize) {
            return false;
        }

        UploadHeapSlot& uploadHeapSlot{ mTextureUploadHeapCollection.Slots[mTextureUploadHeapCollection.CurrentSlotIndex] };
        uint64_t remainingSlotCapacity{ mTextureUploadHeapCollection.BufferSize - uploadHeapSlot.WriteOffset };
        if (remainingSlotCapacity < subresourceBytes) {
            bool switched{ TrySwitchUploadHeapSlot(mTextureUploadHeapCollection, allocatorIndex, requestTouchedMask, requestCompletedSubmitFenceValues) };
            if (switched == false) {
                return false;
            }
        }

        UploadHeapSlot& currentUploadHeapSlot{ mTextureUploadHeapCollection.Slots[mTextureUploadHeapCollection.CurrentSlotIndex] };
        uint64_t destinationUploadOffset{ currentUploadHeapSlot.WriteOffset };
        std::memcpy(currentUploadHeapSlot.UploadHeapMappedData + destinationUploadOffset, copyRequest.SourceData.data() + subresourceOffset, static_cast<size_t>(subresourceBytes));
        currentUploadHeapSlot.WriteOffset += subresourceBytes;

        D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
        sourceLocation.pResource = currentUploadHeapSlot.UploadHeapResource.Get();
        sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        sourceLocation.PlacedFootprint = subresource.Footprint;
        sourceLocation.PlacedFootprint.Offset = destinationUploadOffset;

        D3D12_TEXTURE_COPY_LOCATION destinationLocation{};
        destinationLocation.pResource = copyRequest.DestinationDefaultResource.Get();
        destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destinationLocation.SubresourceIndex = subresource.DestinationSubresourceIndex;

        mCopyCommandList->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);
        requestTouchedMask[requestIndex] = true;
    }

    return true;
}

bool CopyQueue::WriteUploadBytes(UploadHeapCollection& collection, ID3D12Resource* destinationResource, uint64_t destinationOffset, const std::byte* sourceData, uint64_t size, size_t requestIndex, size_t& allocatorIndex, std::vector<bool>& requestTouchedMask, std::vector<uint64_t>& requestCompletedSubmitFenceValues) {
    if (size == 0) {
        return true;
    }

    uint64_t remainingSize{ size };
    uint64_t sourceOffset{ 0 };
    while (remainingSize > 0) {
        UploadHeapSlot& uploadHeapSlot{ collection.Slots[collection.CurrentSlotIndex] };
        uint64_t remainingSlotCapacity{ collection.BufferSize - uploadHeapSlot.WriteOffset };
        if (remainingSlotCapacity == 0) {
            bool switched{ TrySwitchUploadHeapSlot(collection, allocatorIndex, requestTouchedMask, requestCompletedSubmitFenceValues) };
            if (switched == false) {
                return false;
            }
            continue;
        }

        uint64_t chunkSize{ remainingSize > remainingSlotCapacity ? remainingSlotCapacity : remainingSize };
        std::memcpy(uploadHeapSlot.UploadHeapMappedData + uploadHeapSlot.WriteOffset, sourceData + sourceOffset, static_cast<size_t>(chunkSize));
        if (destinationResource != nullptr) {
            mCopyCommandList->CopyBufferRegion(destinationResource, destinationOffset + sourceOffset, uploadHeapSlot.UploadHeapResource.Get(), uploadHeapSlot.WriteOffset, chunkSize);
            requestTouchedMask[requestIndex] = true;
        }

        uploadHeapSlot.WriteOffset += chunkSize;
        sourceOffset += chunkSize;
        remainingSize -= chunkSize;
    }

    return true;
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
    uint64_t requestedFenceValue{ EnqueueCopy(std::span<const Interface::CopyRequest>{}) };
    DispatchCopies();
    WaitForFence(requestedFenceValue);
}

bool CopyQueue::IsSubmitFenceComplete(uint64_t fenceValue) const {
    return mCopyFence->GetCompletedValue() >= fenceValue;
}

void CopyQueue::WaitForSubmitFence(uint64_t fenceValue) const {
    if (IsSubmitFenceComplete(fenceValue) == true) {
        return;
    }

    std::lock_guard<std::mutex> fenceGuard{ mFenceMutex };
    ErrorHandler::report(mCopyFence->SetEventOnCompletion(fenceValue, mFenceEvent), "CopyQueue", "Failed to set copy queue fence completion event.", ErrorHandler::Level::Critical);
    WaitForSingleObjectEx(mFenceEvent, INFINITE, FALSE);
}

uint64_t CopyQueue::ResolveRequestedFenceValue(uint64_t fenceValue) const {
    std::unique_lock<std::mutex> fenceGuard{ mFenceMutex };
    std::unordered_map<uint64_t, uint64_t>::const_iterator foundFence{ mRequestedFenceToCompletedSubmitFence.find(fenceValue) };
    if (foundFence == mRequestedFenceToCompletedSubmitFence.end()) {
        return fenceValue;
    }

    mFenceCondition.wait(fenceGuard, [this, fenceValue] {
        std::unordered_map<uint64_t, uint64_t>::const_iterator currentFence{ mRequestedFenceToCompletedSubmitFence.find(fenceValue) };
        if (currentFence == mRequestedFenceToCompletedSubmitFence.end()) {
            return true;
        }

        return currentFence->second != PendingSubmitFenceValue;
    });

    foundFence = mRequestedFenceToCompletedSubmitFence.find(fenceValue);
    if (foundFence == mRequestedFenceToCompletedSubmitFence.end()) {
        return fenceValue;
    }

    return foundFence->second;
}

void CopyQueue::PrepareAllocatorForRecording(size_t allocatorIndex) {
    uint64_t allocatorFenceValue{ mAllocatorFenceValues[allocatorIndex] };
    if (allocatorFenceValue > 0) {
        WaitForSubmitFence(allocatorFenceValue);
    }

    ErrorHandler::report(mCopyCommandAllocators[allocatorIndex]->Reset(), "CopyQueue", "Failed to reset copy command allocator.", ErrorHandler::Level::Critical);
    ErrorHandler::report(mCopyCommandList->Reset(mCopyCommandAllocators[allocatorIndex].Get(), nullptr), "CopyQueue", "Failed to reset copy command list.", ErrorHandler::Level::Critical);
}

uint64_t CopyQueue::SubmitCurrentCommandList(size_t allocatorIndex, std::vector<bool>& requestTouchedMask, std::vector<uint64_t>& requestCompletedSubmitFenceValues) {
    ErrorHandler::report(mCopyCommandList->Close(), "CopyQueue", "Failed to close copy command list.", ErrorHandler::Level::Critical);

    ID3D12CommandList* commandLists[]{ mCopyCommandList.Get() };
    mCopyCommandQueue->ExecuteCommandLists(1, commandLists);

    uint64_t submitFenceValue{ mSubmitFenceValueCounter.fetch_add(1) + 1 };
    {
        std::lock_guard<std::mutex> fenceGuard{ mFenceMutex };
        ErrorHandler::report(mCopyCommandQueue->Signal(mCopyFence.Get(), submitFenceValue), "CopyQueue", "Failed to signal copy queue fence.", ErrorHandler::Level::Critical);
    }

    mAllocatorFenceValues[allocatorIndex] = submitFenceValue;
    mBufferUploadHeapCollection.Slots[mBufferUploadHeapCollection.CurrentSlotIndex].SlotFenceValue = submitFenceValue;
    mTextureUploadHeapCollection.Slots[mTextureUploadHeapCollection.CurrentSlotIndex].SlotFenceValue = submitFenceValue;

    for (size_t requestIndex{ 0 }; requestIndex < requestTouchedMask.size(); requestIndex++) {
        if (requestTouchedMask[requestIndex] == true && requestCompletedSubmitFenceValues[requestIndex] < submitFenceValue) {
            requestCompletedSubmitFenceValues[requestIndex] = submitFenceValue;
            requestTouchedMask[requestIndex] = false;
        }
    }

    return submitFenceValue;
}

bool CopyQueue::TrySwitchUploadHeapSlot(UploadHeapCollection& collection, size_t& allocatorIndex, std::vector<bool>& requestTouchedMask, std::vector<uint64_t>& requestCompletedSubmitFenceValues) {
    uint64_t submittedFenceValue{ SubmitCurrentCommandList(allocatorIndex, requestTouchedMask, requestCompletedSubmitFenceValues) };
    if (submittedFenceValue == 0) {
        return false;
    }

    collection.CurrentSlotIndex = (collection.CurrentSlotIndex + 1) % collection.Slots.size();
    UploadHeapSlot& nextUploadHeapSlot{ collection.Slots[collection.CurrentSlotIndex] };
    if (nextUploadHeapSlot.SlotFenceValue > 0) {
        WaitForSubmitFence(nextUploadHeapSlot.SlotFenceValue);
    }

    nextUploadHeapSlot.WriteOffset = 0;

    allocatorIndex = (allocatorIndex + 1) % CopyAllocatorCount;
    PrepareAllocatorForRecording(allocatorIndex);
    return true;
}

bool CopyQueue::InitializeUploadHeapCollection(ID3D12Device* device, UploadHeapCollection& collection, UINT64 bufferSize) {
    collection.BufferSize = bufferSize;
    collection.CurrentSlotIndex = 0;

    D3D12_HEAP_PROPERTIES uploadHeapProperties{};
    uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    uploadHeapProperties.CreationNodeMask = 1;
    uploadHeapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC uploadBufferDescription{};
    uploadBufferDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadBufferDescription.Alignment = 0;
    uploadBufferDescription.Width = bufferSize;
    uploadBufferDescription.Height = 1;
    uploadBufferDescription.DepthOrArraySize = 1;
    uploadBufferDescription.MipLevels = 1;
    uploadBufferDescription.Format = DXGI_FORMAT_UNKNOWN;
    uploadBufferDescription.SampleDesc.Count = 1;
    uploadBufferDescription.SampleDesc.Quality = 0;
    uploadBufferDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uploadBufferDescription.Flags = D3D12_RESOURCE_FLAG_NONE;

    for (UploadHeapSlot& uploadHeapSlot : collection.Slots) {
        uploadHeapSlot.UploadHeapMappedData = nullptr;
        uploadHeapSlot.WriteOffset = 0;
        uploadHeapSlot.SlotFenceValue = 0;
        ErrorHandler::report(device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &uploadBufferDescription, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(uploadHeapSlot.UploadHeapResource.GetAddressOf())), "CopyQueue", "Failed to create copy queue upload buffer.", ErrorHandler::Level::Critical);
        ErrorHandler::report(uploadHeapSlot.UploadHeapResource->Map(0, nullptr, reinterpret_cast<void**>(&uploadHeapSlot.UploadHeapMappedData)), "CopyQueue", "Failed to map copy queue upload buffer.", ErrorHandler::Level::Critical);
    }

    return true;
}

void CopyQueue::ResetUploadHeapCollection(UploadHeapCollection& collection) {
    for (UploadHeapSlot& uploadHeapSlot : collection.Slots) {
        if (uploadHeapSlot.UploadHeapResource != nullptr) {
            uploadHeapSlot.UploadHeapResource->Unmap(0, nullptr);
            uploadHeapSlot.UploadHeapMappedData = nullptr;
        }
        uploadHeapSlot.UploadHeapResource.Reset();
        uploadHeapSlot.WriteOffset = 0;
        uploadHeapSlot.SlotFenceValue = 0;
    }

    collection.BufferSize = 0;
    collection.CurrentSlotIndex = 0;
}
