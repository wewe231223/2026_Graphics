#include "CopyQueue.h"
#include <array>
#include <cstring>
#include <limits>
#include <vector>
#include "Utility/ErrorHandler.h"
#include "Utility/StdOutput.h"

using namespace Core::DX;

CopyQueue::CopyQueue(ID3D12Device* Device)
    : mCopyCommandQueue{},
    mCopyCommandAllocators{},
    mCopyCommandList{},
    mCopyFence{},
    mUploadAllocator{},
    mCurrentAllocatorFlightIndex{ 0 },
    mAllocatorFlightFenceValues{},
    mFenceEvent{},
    mQueueMutex{},
    mFenceMutex{},
    mUploadAllocatorMutex{},
    mQueueCondition{},
    mFenceCondition{},
    mPendingRequestBatches{},
    mInFlightUploads{},
    mDispatchRequested{ false },
    mWorkerThread{},
    mIsRunning{ true },
    mSubmitFenceValueCounter{ 0 },
    mCopyIdFenceStates{} {
    bool InitializeResult{ Initialize(Device) };
    ErrorHandler::report(InitializeResult == false, "CopyQueue", "Failed to initialize copy queue.", ErrorHandler::Level::Critical);
}

CopyQueue::~CopyQueue() {
    StopWorker();

    if (mFenceEvent != nullptr) {
        CloseHandle(mFenceEvent);
        mFenceEvent = nullptr;
    }
}

bool CopyQueue::Initialize(ID3D12Device* Device) {
    if (Device == nullptr) {
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC QueueDescription{};
    QueueDescription.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    QueueDescription.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    QueueDescription.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    QueueDescription.NodeMask = 0;
    ErrorHandler::report(Device->CreateCommandQueue(&QueueDescription, IID_PPV_ARGS(mCopyCommandQueue.GetAddressOf())), "CopyQueue", "Failed to create copy command queue.", ErrorHandler::Level::Critical);

    for (Microsoft::WRL::ComPtr<ID3D12CommandAllocator>& CopyCommandAllocator : mCopyCommandAllocators) {
        ErrorHandler::report(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(CopyCommandAllocator.GetAddressOf())), "CopyQueue", "Failed to create copy command allocator.", ErrorHandler::Level::Critical);
    }

    ErrorHandler::report(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, mCopyCommandAllocators[mCurrentAllocatorFlightIndex].Get(), nullptr, IID_PPV_ARGS(mCopyCommandList.GetAddressOf())), "CopyQueue", "Failed to create copy command list.", ErrorHandler::Level::Critical);
    ErrorHandler::report(mCopyCommandList->Close(), "CopyQueue", "Failed to close initial copy command list.", ErrorHandler::Level::Critical);
    ErrorHandler::report(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(mCopyFence.GetAddressOf())), "CopyQueue", "Failed to create copy queue fence.", ErrorHandler::Level::Critical);

    D3D12_HEAP_PROPERTIES UploadHeapProperties{};
    UploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    UploadHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    UploadHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    UploadHeapProperties.CreationNodeMask = 1;
    UploadHeapProperties.VisibleNodeMask = 1;
    ErrorHandler::report(mUploadAllocator.Initialize(Device, UploadAllocatorHeapSize, UploadHeapProperties) == false, "CopyQueue", "Failed to initialize upload allocator.", ErrorHandler::Level::Critical);

    mFenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    ErrorHandler::report(mFenceEvent == nullptr, "CopyQueue", "Failed to create copy queue fence event.", ErrorHandler::Level::Critical);

    mWorkerThread = std::thread(&CopyQueue::WorkerLoop, this);
    return true;
}

bool CopyQueue::EnqueueCopy(std::uint64_t CopyId, const Interface::CopyQueueCopyRequest& CopyRequest) {
    std::array<Interface::CopyQueueCopyRequest, 1> CopyRequests{ CopyRequest };
    return EnqueueCopy(CopyId, CopyRequests);
}

bool CopyQueue::EnqueueCopy(std::uint64_t CopyId, std::span<const Interface::CopyQueueCopyRequest> CopyRequests) {
    CopyRequestBatch RequestBatch{};
    RequestBatch.CopyId = CopyId;

    bool IsPrepared{ PrepareCopyRequests(CopyRequests, RequestBatch.CopyRequests) };
    if (IsPrepared == false) {
        return false;
    }

    {
        std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
        CopyIdFenceState& FenceState{ mCopyIdFenceStates[CopyId] };
        FenceState.PendingBatchCount += 1;
    }

    {
        std::lock_guard<std::mutex> QueueGuard{ mQueueMutex };
        mPendingRequestBatches.push(std::move(RequestBatch));
    }

    return true;
}

bool CopyQueue::EnqueueTextureCopy(std::uint64_t CopyId, const Interface::CopyQueueTextureCopyRequest& CopyRequest) {
    std::array<Interface::CopyQueueTextureCopyRequest, 1> CopyRequests{ CopyRequest };
    return EnqueueTextureCopy(CopyId, CopyRequests);
}

bool CopyQueue::EnqueueTextureCopy(std::uint64_t CopyId, std::span<const Interface::CopyQueueTextureCopyRequest> CopyRequests) {
    CopyRequestBatch RequestBatch{};
    RequestBatch.CopyId = CopyId;

    bool IsPrepared{ PrepareTextureCopyRequests(CopyRequests, RequestBatch.CopyRequests) };
    if (IsPrepared == false) {
        return false;
    }

    {
        std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
        CopyIdFenceState& FenceState{ mCopyIdFenceStates[CopyId] };
        FenceState.PendingBatchCount += 1;
    }

    {
        std::lock_guard<std::mutex> QueueGuard{ mQueueMutex };
        mPendingRequestBatches.push(std::move(RequestBatch));
    }

    return true;
}

void CopyQueue::DispatchCopies() {
    {
        std::lock_guard<std::mutex> QueueGuard{ mQueueMutex };
        mDispatchRequested = true;
    }

    mQueueCondition.notify_one();
}

bool CopyQueue::IsFenceComplete(std::uint64_t CopyId) const {
    std::uint64_t SubmitFenceValue{ ResolveCopyIdToFenceValue(CopyId) };
    return IsSubmitFenceComplete(SubmitFenceValue);
}

void CopyQueue::GuaranteeCopy(std::uint64_t CopyId) const {
    std::uint64_t SubmitFenceValue{ ResolveCopyIdToFenceValue(CopyId) };
    WaitForSubmitFence(SubmitFenceValue);
}

void CopyQueue::Flush() {
    DispatchCopies();
    WaitForQueueIdle();
}

std::uint64_t CopyQueue::GetRequiredUploadBufferSize() const {
    return UploadAllocatorHeapSize;
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

void CopyQueue::ExecuteRequestBatch(CopyRequestBatch& RequestBatch) {
    CollectCompletedUploads();

    if (RequestBatch.CopyRequests.empty() == true) {
        {
            std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
            CopyIdFenceState& FenceState{ mCopyIdFenceStates[RequestBatch.CopyId] };
            FenceState.LastSubmitFenceValue = mSubmitFenceValueCounter.load();
            if (FenceState.PendingBatchCount > 0) {
                FenceState.PendingBatchCount -= 1;
            }
        }

        mFenceCondition.notify_all();
        return;
    }

    std::uint32_t AllocatorFlightIndex{ mCurrentAllocatorFlightIndex };
    std::uint64_t AllocatorFenceValue{ mAllocatorFlightFenceValues[AllocatorFlightIndex] };
    if (AllocatorFenceValue > 0 and IsSubmitFenceComplete(AllocatorFenceValue) == false) {
        WaitForSubmitFence(AllocatorFenceValue);
    }

    ErrorHandler::report(mCopyCommandAllocators[AllocatorFlightIndex]->Reset(), "CopyQueue", "Failed to reset copy command allocator.", ErrorHandler::Level::Critical);
    ErrorHandler::report(mCopyCommandList->Reset(mCopyCommandAllocators[AllocatorFlightIndex].Get(), nullptr), "CopyQueue", "Failed to reset copy command list.", ErrorHandler::Level::Critical);

    std::vector<UploadAllocation> BatchUploads{};
    BatchUploads.reserve(RequestBatch.CopyRequests.size());

    for (PreparedCopyRequest& PreparedCopyRequest : RequestBatch.CopyRequests) {
        ID3D12Resource* UploadResource{ PreparedCopyRequest.Upload.AllocationHandle->GetResource() };

        if (PreparedCopyRequest.IsTextureCopy == true) {
            UINT SubresourceCount{ static_cast<UINT>(PreparedCopyRequest.Layouts.size()) };
            for (UINT SubresourceIndex = 0; SubresourceIndex < SubresourceCount; ++SubresourceIndex) {
                D3D12_TEXTURE_COPY_LOCATION DestinationLocation{};
                DestinationLocation.pResource = PreparedCopyRequest.DestinationDefaultResource.Get();
                DestinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                DestinationLocation.SubresourceIndex = SubresourceIndex;

                D3D12_TEXTURE_COPY_LOCATION SourceLocation{};
                SourceLocation.pResource = UploadResource;
                SourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                SourceLocation.PlacedFootprint = PreparedCopyRequest.Layouts[SubresourceIndex];

                mCopyCommandList->CopyTextureRegion(&DestinationLocation, 0, 0, 0, &SourceLocation, nullptr);
            }
        }
        else {
            mCopyCommandList->CopyBufferRegion(PreparedCopyRequest.DestinationDefaultResource.Get(), PreparedCopyRequest.DestinationOffset, UploadResource, 0, PreparedCopyRequest.CopySize);
        }

        BatchUploads.push_back(std::move(PreparedCopyRequest.Upload));
    }

    ErrorHandler::report(mCopyCommandList->Close(), "CopyQueue", "Failed to close copy command list.", ErrorHandler::Level::Critical);

    ID3D12CommandList* CommandLists[]{ mCopyCommandList.Get() };
    mCopyCommandQueue->ExecuteCommandLists(1, CommandLists);

    std::uint64_t SubmitFenceValue{ mSubmitFenceValueCounter.fetch_add(1) + 1 };
    ErrorHandler::report(mCopyCommandQueue->Signal(mCopyFence.Get(), SubmitFenceValue), "CopyQueue", "Failed to signal copy queue fence.", ErrorHandler::Level::Critical);
    mAllocatorFlightFenceValues[AllocatorFlightIndex] = SubmitFenceValue;
    mCurrentAllocatorFlightIndex = (mCurrentAllocatorFlightIndex + 1) % CopyAllocatorFlightCount;

    {
        std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
        CopyIdFenceState& FenceState{ mCopyIdFenceStates[RequestBatch.CopyId] };
        if (FenceState.LastSubmitFenceValue < SubmitFenceValue) {
            FenceState.LastSubmitFenceValue = SubmitFenceValue;
        }

        if (FenceState.PendingBatchCount > 0) {
            FenceState.PendingBatchCount -= 1;
        }
    }

    InFlightUpload UploadBatch{};
    UploadBatch.SubmitFenceValue = SubmitFenceValue;
    UploadBatch.Uploads = std::move(BatchUploads);
    mInFlightUploads.push_back(std::move(UploadBatch));

    mFenceCondition.notify_all();
    CollectCompletedUploads();
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

    CollectCompletedUploads();
}

void CopyQueue::WaitForQueueIdle() {
    static constexpr std::uint64_t IdleCopyId{ std::numeric_limits<std::uint64_t>::max() };
    bool EnqueueResult{ EnqueueCopy(IdleCopyId, std::span<const Interface::CopyQueueCopyRequest>{}) };
    ErrorHandler::report(EnqueueResult == false, "CopyQueue", "Failed to enqueue idle marker.", ErrorHandler::Level::Critical);
    DispatchCopies();
    GuaranteeCopy(IdleCopyId);
}

bool CopyQueue::IsSubmitFenceComplete(std::uint64_t FenceValue) const {
    return mCopyFence->GetCompletedValue() >= FenceValue;
}

void CopyQueue::WaitForSubmitFence(std::uint64_t FenceValue) const {
    if (IsSubmitFenceComplete(FenceValue) == true) {
        return;
    }

	StdOutput::PrintErrorLine("Waiting for copy queue fence value {} to be completed.", FenceValue);

    std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };

    ErrorHandler::report(mCopyFence->SetEventOnCompletion(FenceValue, mFenceEvent), "CopyQueue", "Failed to set copy queue fence completion event.", ErrorHandler::Level::Critical);
    WaitForSingleObjectEx(mFenceEvent, INFINITE, FALSE);
}

std::uint64_t CopyQueue::ResolveCopyIdToFenceValue(std::uint64_t CopyId) const {
    std::unique_lock<std::mutex> FenceGuard{ mFenceMutex };
    std::unordered_map<std::uint64_t, CopyIdFenceState>::const_iterator FoundFence{ mCopyIdFenceStates.find(CopyId) };
    if (FoundFence == mCopyIdFenceStates.end()) {
        return CopyId;
    }

    mFenceCondition.wait(FenceGuard, [this, CopyId] {
        std::unordered_map<std::uint64_t, CopyIdFenceState>::const_iterator CurrentFence{ mCopyIdFenceStates.find(CopyId) };
        if (CurrentFence == mCopyIdFenceStates.end()) {
            return true;
        }

        return CurrentFence->second.PendingBatchCount == 0;
    });

    FoundFence = mCopyIdFenceStates.find(CopyId);
    if (FoundFence == mCopyIdFenceStates.end()) {
        return CopyId;
    }

    return FoundFence->second.LastSubmitFenceValue;
}

bool CopyQueue::PrepareCopyRequests(std::span<const Interface::CopyQueueCopyRequest> CopyRequests, std::vector<PreparedCopyRequest>& OutPreparedRequests) {
    std::vector<PreparedCopyRequest> PreparedRequests{};
    PreparedRequests.reserve(CopyRequests.size());

    std::lock_guard<std::mutex> UploadAllocatorGuard{ mUploadAllocatorMutex };

    for (const Interface::CopyQueueCopyRequest& CopyRequest : CopyRequests) {
        if (CopyRequest.DestinationDefaultResource == nullptr) {
            return false;
        }

        if (CopyRequest.SourceData.empty() == true) {
            continue;
        }

        D3D12_RESOURCE_DESC UploadResourceDescription{};
        UploadResourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        UploadResourceDescription.Alignment = 0;
        UploadResourceDescription.Width = static_cast<UINT64>(CopyRequest.SourceData.size());
        UploadResourceDescription.Height = 1;
        UploadResourceDescription.DepthOrArraySize = 1;
        UploadResourceDescription.MipLevels = 1;
        UploadResourceDescription.Format = DXGI_FORMAT_UNKNOWN;
        UploadResourceDescription.SampleDesc.Count = 1;
        UploadResourceDescription.SampleDesc.Quality = 0;
        UploadResourceDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        UploadResourceDescription.Flags = D3D12_RESOURCE_FLAG_NONE;

        std::unique_ptr<Interface::IAllocationHandle> UploadAllocationHandle{ mUploadAllocator.AllocatePlacedResource(UploadResourceDescription, D3D12_RESOURCE_STATE_GENERIC_READ) };
        if (UploadAllocationHandle == nullptr or UploadAllocationHandle->IsValid() == false) {
            return false;
        }

        void* UploadMappedData{};
        ErrorHandler::report(UploadAllocationHandle->GetResource()->Map(0, nullptr, &UploadMappedData), "CopyQueue", "Failed to map upload resource.", ErrorHandler::Level::Critical);
        std::memcpy(UploadMappedData, CopyRequest.SourceData.data(), CopyRequest.SourceData.size());
        UploadAllocationHandle->GetResource()->Unmap(0, nullptr);

        PreparedCopyRequest NewPreparedCopyRequest{};
        NewPreparedCopyRequest.IsTextureCopy = false;
        NewPreparedCopyRequest.DestinationDefaultResource = CopyRequest.DestinationDefaultResource;
        NewPreparedCopyRequest.DestinationOffset = CopyRequest.DestinationOffset;
        NewPreparedCopyRequest.CopySize = static_cast<std::uint64_t>(CopyRequest.SourceData.size());
        NewPreparedCopyRequest.Upload.AllocationHandle = std::move(UploadAllocationHandle);
        PreparedRequests.push_back(std::move(NewPreparedCopyRequest));
    }

    OutPreparedRequests = std::move(PreparedRequests);
    return true;
}

bool CopyQueue::PrepareTextureCopyRequests(std::span<const Interface::CopyQueueTextureCopyRequest> CopyRequests, std::vector<PreparedCopyRequest>& OutPreparedRequests) {
    std::vector<PreparedCopyRequest> PreparedRequests{};
    PreparedRequests.reserve(CopyRequests.size());

    std::lock_guard<std::mutex> UploadAllocatorGuard{ mUploadAllocatorMutex };

    for (const Interface::CopyQueueTextureCopyRequest& CopyRequest : CopyRequests) {
        if (CopyRequest.DestinationTextureResource == nullptr) {
            return false;
        }

        if (CopyRequest.SourceLayouts.empty() == true or CopyRequest.SourceData.empty() == true) {
            continue;
        }

        D3D12_RESOURCE_DESC UploadResourceDescription{};
        UploadResourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        UploadResourceDescription.Alignment = 0;
        UploadResourceDescription.Width = static_cast<UINT64>(CopyRequest.SourceData.size());
        UploadResourceDescription.Height = 1;
        UploadResourceDescription.DepthOrArraySize = 1;
        UploadResourceDescription.MipLevels = 1;
        UploadResourceDescription.Format = DXGI_FORMAT_UNKNOWN;
        UploadResourceDescription.SampleDesc.Count = 1;
        UploadResourceDescription.SampleDesc.Quality = 0;
        UploadResourceDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        UploadResourceDescription.Flags = D3D12_RESOURCE_FLAG_NONE;

        std::unique_ptr<Interface::IAllocationHandle> UploadAllocationHandle{ mUploadAllocator.AllocatePlacedResource(UploadResourceDescription, D3D12_RESOURCE_STATE_GENERIC_READ) };
        if (UploadAllocationHandle == nullptr or UploadAllocationHandle->IsValid() == false) {
            return false;
        }

        void* UploadMappedData{};
        ErrorHandler::report(UploadAllocationHandle->GetResource()->Map(0, nullptr, &UploadMappedData), "CopyQueue", "Failed to map texture upload resource.", ErrorHandler::Level::Critical);
        std::memcpy(UploadMappedData, CopyRequest.SourceData.data(), CopyRequest.SourceData.size());
        UploadAllocationHandle->GetResource()->Unmap(0, nullptr);

        PreparedCopyRequest NewPreparedCopyRequest{};
        NewPreparedCopyRequest.IsTextureCopy = true;
        NewPreparedCopyRequest.DestinationDefaultResource = CopyRequest.DestinationTextureResource;
        NewPreparedCopyRequest.CopySize = static_cast<std::uint64_t>(CopyRequest.SourceData.size());
        NewPreparedCopyRequest.Layouts = CopyRequest.SourceLayouts;
        NewPreparedCopyRequest.Upload.AllocationHandle = std::move(UploadAllocationHandle);
        PreparedRequests.push_back(std::move(NewPreparedCopyRequest));
    }

    OutPreparedRequests = std::move(PreparedRequests);
    return true;
}

void CopyQueue::CollectCompletedUploads() {
    std::lock_guard<std::mutex> UploadAllocatorGuard{ mUploadAllocatorMutex };

    while (mInFlightUploads.empty() == false) {
        if (IsSubmitFenceComplete(mInFlightUploads.front().SubmitFenceValue) == false) {
            break;
        }

        mInFlightUploads.pop_front();
    }
}
